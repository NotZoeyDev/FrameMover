#include "app.h"
#include "backup.h"
#include "files.h"
#include "vr_math.h"

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using namespace vr;

std::atomic<bool> App::s_quit{ false };
std::atomic<bool> App::s_showDashboard{ false };

namespace
{
constexpr double kResetHoldSeconds = 1.5;
constexpr double kRebaseDebounce = 0.3;
constexpr double kResumeAfterPause = 3.0; // seconds before looking at the saved playspace again after another program edited it
constexpr double kRoomSetupTimeout = 90.0; // seconds of waiting for SteamVR's room setup to report back
constexpr double kReanchorEcho = 3.0;      // seconds during which commit events are taken as the echo of our own re-anchor
constexpr double kDegToRad = M_PI / 180.0;
// The universe id SteamVR gives the dummy chaperone while the headset is not tracking ("Switch to the dummy chaperone universe").
constexpr uint64_t kDummyUniverse = 424242;

double Now()
{
	return std::chrono::duration<double>( std::chrono::steady_clock::now().time_since_epoch() ).count();
}
} // namespace

std::string FindDataDir()
{
	char exe[4096];
	const ssize_t n = readlink( "/proc/self/exe", exe, sizeof exe - 1 );
	std::string dir;
	if ( n > 0 )
	{
		dir.assign( exe, (size_t)n );
		dir = dir.substr( 0, dir.rfind( '/' ) );
	}
	for ( const std::string &c : { dir + "/data", dir + "/../data", files::Home() + "/.local/share/frame-mover" } )
		if ( access( ( c + "/action_manifest.json" ).c_str(), R_OK ) == 0 )
			return c;
	return files::Home() + "/.local/share/frame-mover";
}

App::~App()
{
	if ( m_vrInit )
	{
		m_chaperone.Restore(); // the world goes back to the saved playspace when the mover exits
		if ( m_panel != k_ulOverlayHandleInvalid )
		{
			// Take the panel down while we are still connected. Vanishing with an upload in flight made the
			// compositor read freed shared memory and crash (SIGBUS), restarting SteamVR.
			VROverlay()->HideOverlay( m_panel );
			VROverlay()->DestroyOverlay( m_panel );
			std::this_thread::sleep_for( std::chrono::milliseconds( 300 ) );
		}
		// OpenVR's client library creates its own Vulkan objects (a query pool) on the device we hand it textures
		// from, and frees them inside VR_Shutdown. Our device must therefore outlive VR_Shutdown: the Vulkan members
		// (declared m_vk, then m_panelTex) are destroyed after this body, texture first.
		VR_Shutdown();
	}
}

bool App::Init( std::string &error )
{
	EVRInitError err = VRInitError_None;
	VR_Init( &err, VRApplication_Overlay );
	if ( err != VRInitError_None )
	{
		error = std::string( "VR_Init failed: " ) + VR_GetVRInitErrorAsEnglishDescription( err );
		return false;
	}
	m_vrInit = true;
	if ( !VROverlay() || !VRSystem() || !VRChaperoneSetup() )
	{
		error = "SteamVR overlay/chaperone interfaces unavailable";
		return false;
	}
	m_settings = LoadSettings();

	const std::string manifest = FindDataDir() + "/action_manifest.json";
	m_inputOk = m_actions.Init( manifest, m_settings.overrideGameInput, m_inputError );
	if ( !m_inputOk )
		fprintf( stderr, "SteamVR Input unavailable: %s\n", m_inputError.c_str() );

	EVROverlayError oe = VROverlay()->CreateDashboardOverlay( kPanelKey, "FrameMover", &m_panel, &m_thumb );
	if ( oe != VROverlayError_None )
	{
		error = std::string( "CreateDashboardOverlay failed: " ) + VROverlay()->GetOverlayErrorNameFromEnum( oe );
		return false;
	}
	VROverlay()->SetOverlayWidthInMeters( m_panel, 2.4f );
	VROverlay()->SetOverlayFlag( m_panel, VROverlayFlags_EnableControlBarClose, true );
	VROverlay()->SetOverlayInputMethod( m_panel, VROverlayInputMethod_Mouse );
	HmdVector2_t scale;
	scale.v[0] = (float)kPanelW;
	scale.v[1] = (float)kPanelH;
	VROverlay()->SetOverlayMouseScale( m_panel, &scale );
	{
		Canvas icon( 128, 128 );
		DrawGlyph( icon, 64, 64, 112 );
		std::vector<uint8_t> px( 128 * 128 * 4 );
		icon.CopyRGBA( px.data() );
		VROverlay()->SetOverlayRaw( m_thumb, px.data(), 128, 128, 4 );
	}

	// The panel changes while it is on screen. SetOverlayRaw makes an overlay vanish for a couple of frames on every call
	// (visible as flicker), so it is handed over as a Vulkan texture with SetOverlayTexture.
	std::string vkErr;
	m_vk = std::make_unique<VulkanContext>();
	if ( m_vk->Init( vkErr ) )
		m_panelTex = std::make_unique<OverlayTexture>( *m_vk );
	else
	{
		fprintf( stderr, "Vulkan setup failed (%s); the panel will flicker (SetOverlayRaw fallback)\n", vkErr.c_str() );
		m_vk.reset();
	}

	Rebase();
	m_universe = CurrentUniverse();
	return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// Playspace state

void App::Rebase()
{
	m_rebaseAt = -1;
	m_rebaseDropsMove = false;
	for ( int h = 0; h < 2; h++ )
	{
		m_drag[h] = DragGesture();
		m_turn[h] = TurnGesture();
	}
	Mat34 standing, seated;
	if ( m_chaperone.Capture( standing, seated ) )
	{
		m_playspace.Rebase( standing, seated );
		m_state = MoverState::Ready;
	}
	else
		m_state = MoverState::WaitingForChaperone;
	m_appliedAny = false;
	m_appliedOffset = {};
	m_appliedYaw = 0;
	m_panelDirty = true;
}

// SteamVR switched to another tracking universe (relocalization against a saved map, a new tracking world) or reloaded the
// saved one. What we captured is stale and SteamVR has dropped our preview, but the move is still what the user wants:
// capture the new saved playspace and put the same move on top of it.
void App::Rebaseline()
{
	if ( !m_playspace.Moved() )
	{
		Rebase();
		return;
	}
	m_rebaseAt = -1;
	m_rebaseDropsMove = false;
	Mat34 standing, seated;
	if ( m_chaperone.Capture( standing, seated, true ) )
	{
		m_playspace.Rebaseline( standing, seated );
		m_state = MoverState::Ready;
	}
	else
	{
		m_playspace.Reset();
		m_state = MoverState::WaitingForChaperone;
	}
	m_appliedAny = false; // ApplyIfChanged() writes the move again this tick
	m_panelDirty = true;
}

void App::ScheduleRebase( bool dropMove )
{
	m_rebaseAt = Now() + kRebaseDebounce;
	m_rebaseDropsMove = m_rebaseDropsMove || dropMove;
}

uint64_t App::CurrentUniverse() const
{
	ETrackedPropertyError err = TrackedProp_Success;
	const uint64_t id = VRSystem()->GetUint64TrackedDeviceProperty( k_unTrackedDeviceIndex_Hmd, Prop_CurrentUniverseId_Uint64, &err );
	return err == TrackedProp_Success ? id : 0;
}

// On the Frame a switch of the tracking universe is only announced with VREvent_ChaperoneFlushCache, and SteamVR throws away
// the live preview (zero pose and bounds) when it happens. The id of the universe in use is the only reliable signal.
void App::CheckUniverse()
{
	const uint64_t id = CurrentUniverse();
	if ( id == 0 || id == m_universe )
		return;
	const uint64_t previous = m_universe;
	m_universe = id;
	if ( previous == 0 || m_state == MoverState::PausedBySetup )
		return; // first reading; or SteamVR's room setup is running and will announce its result itself
	// Losing tracking (the placeholder universe) and coming back starts a new tracking world: raw tracking space is not the
	// same any more, so a move made in the old one means nothing. Switching between real universes (relocalizing against a
	// saved map) keeps raw space, so the move survives.
	const bool newWorld = id == kDummyUniverse || previous == kDummyUniverse;
	fprintf( stderr, "tracking universe %llu -> %llu%s\n", (unsigned long long)previous, (unsigned long long)id,
		newWorld ? " (tracking world changed: dropping the move)" : "" );
	if ( newWorld )
		m_reanchorTried = false; // the next relocalization may need it again
	ScheduleRebase( newWorld );
}

void App::Pause( bool restore )
{
	if ( restore )
		m_chaperone.Restore();
	m_playspace.Reset();
	m_appliedAny = false;
	m_state = MoverState::PausedBySetup;
	m_panelDirty = true;
}

void App::ApplyIfChanged()
{
	const Vec3 off = m_playspace.Offset();
	const double yaw = m_playspace.YawDegrees();
	if ( m_appliedAny && off.x == m_appliedOffset.x && off.y == m_appliedOffset.y && off.z == m_appliedOffset.z && yaw == m_appliedYaw )
		return;
	if ( !m_playspace.Moved() )
	{
		if ( m_chaperone.PreviewShown() )
			Rebase();
		return;
	}
	if ( !m_playspace.InRange() )
	{
		fprintf( stderr, "playspace moved out of the range OpenVR accepts; resetting to the original\n" );
		Rebase();
		return;
	}
	if ( m_chaperone.Apply( m_playspace.Standing(), m_playspace.Seated(), m_settings.largeChaperone, m_settings.disableChaperone ) )
	{
		m_appliedAny = true;
		m_appliedOffset = off;
		m_appliedYaw = yaw;
		m_lastApply = Now();
		m_panelDirty = true;
	}
}

// SteamVR can replace the live zero pose with the saved one without telling apps (its driver re-reads the chaperone file
// in the background). The working copy still holds our values then, so nothing else notices. The only truth is what the
// universe really uses, which we recover from the headset's pose in raw space and in the standing universe. The huge
// play area is checked separately: it is dropped together with the zero pose, but a zero pose can look right by chance.
void App::CheckLivePreview( double now )
{
	if ( !m_chaperone.PreviewShown() || !m_playspace.Moved() || m_rebaseAt >= 0 || now < m_healBackoffUntil )
		return;
	if ( now - m_lastApply < 0.6 || GestureActive() )
		return; // just written, or being moved right now: the next write fixes it anyway
	const Mat34 want = m_playspace.Standing();
	Mat34 live = want;
	const bool poseDiffers = LiveStanding( live ) && !NearPose( live, want, 0.01, 0.01 );
	const bool boundsDropped = m_chaperone.BoundsDropped();
	if ( !poseDiffers && !boundsDropped )
		return;

	const bool others = m_chaperone.ModifiedByOthers();
	fprintf( stderr, "live playspace differs from ours %.1f s after our last write (%s, working copy %s ours)\n", now - m_lastApply,
		poseDiffers ? ( boundsDropped ? "zero pose and play area" : "zero pose" ) : "play area", others ? "NOT" : "still" );
	if ( others )
		return; // someone else edited the working copy: the existing "another program" path handles that
	if ( poseDiffers && OnlyHeightDiffers( live, want, 0.01, 0.01 ) && !m_reanchorTried )
	{
		Reanchor( now );
		return;
	}
	m_healTimes.erase( std::remove_if( m_healTimes.begin(), m_healTimes.end(), [now]( double t ) { return now - t > 20.0; } ), m_healTimes.end() );
	if ( m_healTimes.size() >= 3 )
	{
		m_healBackoffUntil = now + 30.0;
		m_healTimes.clear();
		fprintf( stderr, "SteamVR keeps replacing the playspace; not re-applying for 30 s\n" );
		return;
	}
	m_healTimes.push_back( now );
	if ( m_chaperone.Apply( m_playspace.Standing(), m_playspace.Seated(), m_settings.largeChaperone, m_settings.disableChaperone ) )
	{
		m_lastApply = now;
		fprintf( stderr, "re-applied the playspace SteamVR had dropped\n" );
	}
}

// After the Frame relocalizes into a playspace saved against an older tracking map, SteamVR keeps writing that playspace's
// floor height into the live standing origin many times a second (x, z and yaw stay ours), so no move can change the height.
// Committing the saved playspace unchanged, as Accept in SteamVR's playspace setup does, re-anchors it to the current map
// and that stops. Once per tracking world: if it does not help, the plain re-apply and its back-off take over.
void App::Reanchor( double now )
{
	m_reanchorTried = true;
	fprintf( stderr, "SteamVR is holding the floor height of a playspace anchored to an older tracking map; re-anchoring it to the current map\n" );
	const std::string backups = BackupChaperoneFile(); // the file is about to be written: keep what it was
	if ( backups.empty() )
	{
		fprintf( stderr, "could not back up chaperone_info.vrchap; not re-anchoring\n" );
		return;
	}
	const bool ok = m_chaperone.Reanchor();
	m_appliedAny = false; // the preview is down either way: the move is written again after the rebase
	m_reanchoredAt = now;
	fprintf( stderr, "%s\n", ok ? "re-anchored; putting the move back on top" : "SteamVR rejected the commit; putting the move back" );
	ScheduleRebase( false );
}

bool App::LiveStanding( Mat34 &out ) const
{
	TrackedDevicePose_t raw[k_unTrackedDeviceIndex_Hmd + 1], stand[k_unTrackedDeviceIndex_Hmd + 1];
	VRSystem()->GetDeviceToAbsoluteTrackingPose( TrackingUniverseRawAndUncalibrated, 0.0f, raw, k_unTrackedDeviceIndex_Hmd + 1 );
	VRSystem()->GetDeviceToAbsoluteTrackingPose( TrackingUniverseStanding, 0.0f, stand, k_unTrackedDeviceIndex_Hmd + 1 );
	const TrackedDevicePose_t &r = raw[k_unTrackedDeviceIndex_Hmd], &s = stand[k_unTrackedDeviceIndex_Hmd];
	if ( !r.bPoseIsValid || !s.bPoseIsValid || r.eTrackingResult != TrackingResult_Running_OK || s.eTrackingResult != TrackingResult_Running_OK )
		return false;
	out = ZeroPoseFromPoses( MatrixCast<Mat34>( r.mDeviceToAbsoluteTracking ), MatrixCast<Mat34>( s.mDeviceToAbsoluteTracking ) );
	return true;
}

void App::Pulse( bool bothHands )
{
	if ( !m_settings.haptics || !m_inputOk )
		return;
	m_actions.Pulse( Hand::Right );
	if ( bothHands )
		m_actions.Pulse( Hand::Left );
}

App::Tracked App::ReadPose( TrackedDeviceIndex_t idx, const TrackedDevicePose_t *poses ) const
{
	Tracked t;
	if ( idx >= k_unMaxTrackedDeviceCount )
		return t;
	const TrackedDevicePose_t &p = poses[idx];
	t.valid = p.bPoseIsValid && p.bDeviceIsConnected && p.eTrackingResult == TrackingResult_Running_OK;
	t.pos = { p.mDeviceToAbsoluteTracking.m[0][3], p.mDeviceToAbsoluteTracking.m[1][3], p.mDeviceToAbsoluteTracking.m[2][3] };
	return t;
}

// ---------------------------------------------------------------------------------------------------------------------
// Events

void App::HandleSystemEvent( const VREvent_t &ev )
{
	switch ( ev.eventType )
	{
	case VREvent_Quit:
		VRSystem()->AcknowledgeQuit_Exiting();
		s_quit = true;
		break;
	case VREvent_ChaperoneRoomSetupStarting:
		// SteamVR's own room setup edits the same working copy; step aside and put everything back first. A committed setup
		// ends the pause through its own events; a cancelled one sends none, so look again after a while rather than stay
		// paused until the next restart.
		Pause( true );
		m_rebaseAt = Now() + kRoomSetupTimeout;
		m_rebaseDropsMove = true;
		break;
	case VREvent_SeatedZeroPoseReset:
	case VREvent_StandingZeroPoseReset:
		// SteamVR sends these often. Right after we apply a move they can be an echo of our own preview; adopting
		// them as a new original would silently cancel the move.
		if ( m_chaperone.PreviewShown() && Now() - m_lastApply < 0.5 && !m_chaperone.ModifiedByOthers() )
			break;
		[[fallthrough]];
	case 807: // RoomSetupCommitted in current OpenVR, formerly RoomSetupFinished.
		if ( Now() - m_reanchoredAt < kReanchorEcho )
		{
			// Our own commit announces itself like a finished room setup; the move stays.
			ScheduleRebase( false );
			break;
		}
		// The user recentered or finished room setup: whatever we held is stale and so is the move. Adopt the new
		// playspace as the original once the burst of events is over.
		fprintf( stderr, "playspace changed outside the mover (%s); adopting it as the new original\n",
			VRSystem()->GetEventTypeNameFromEnum( (EVREventType)ev.eventType ) );
		ScheduleRebase( true );
		break;
	case VREvent_ChaperoneUniverseHasChanged:
	case VREvent_ChaperoneDataHasChanged:
		// SteamVR relocalized against a saved map or reloaded the chaperone: the saved playspace is new, but the user did
		// not ask for that, so their move stays.
		fprintf( stderr, "SteamVR switched the saved playspace (%s)\n", VRSystem()->GetEventTypeNameFromEnum( (EVREventType)ev.eventType ) );
		ScheduleRebase( false );
		break;
	case VREvent_ChaperoneFlushCache:
		m_nextUniverseCheck = 0; // how a new tracking universe announces itself on the Frame: read the universe id now
		break;
	case VREvent_ActionBindingReloaded:
	case VREvent_Input_BindingLoadSuccessful:
	case VREvent_Input_ActionManifestReloaded:
		fprintf( stderr, "SteamVR reloaded the bindings (%s)\n", VRSystem()->GetEventTypeNameFromEnum( (EVREventType)ev.eventType ) );
		m_bindingsStale = true; // the user changed the bindings in SteamVR: re-read them
		m_pinBindingsAt = Now() + 1.5; // and if they were saved just now, keep a local copy so a later start never needs Steam for them
		break;
	case VREvent_TrackedDeviceActivated:
	case VREvent_TrackedDeviceDeactivated:
	case VREvent_TrackedDeviceUpdated:
	case VREvent_TrackedDeviceRoleChanged:
		m_bindingsStale = m_panelDirty = true;
		break;
	default:
		break;
	}
}

void App::HandlePanelEvent( const VREvent_t &ev )
{
	switch ( ev.eventType )
	{
	case VREvent_MouseMove:
	{
		const int hover = m_buttons.HitTest( ev.data.mouse.x, kPanelH - ev.data.mouse.y ); // overlay mouse origin is bottom-left
		if ( hover != m_hover )
		{
			m_hover = hover;
			m_panelDirty = true;
		}
		break;
	}
	case VREvent_MouseButtonDown:
	{
		if ( !( ev.data.mouse.button & VRMouseButton_Left ) )
			break;
		const int idx = m_buttons.HitTest( ev.data.mouse.x, kPanelH - ev.data.mouse.y );
		if ( idx >= 0 )
		{
			const Button &button = m_buttons.buttons()[idx];
			HandlePanelAction( button.action, button.value );
			m_panelDirty = true;
		}
		break;
	}
	case VREvent_MouseButtonUp:
	case VREvent_OverlayShown:
		m_bindingsStale = true; // coming back from the binding editor: show the new bindings
		m_panelDirty = true;
		break;
	case VREvent_OverlayClosed:
		s_quit = true; // the dashboard's Close button
		break;
	default:
		break;
	}
}

void App::PollEvents()
{
	VREvent_t ev;
	while ( VROverlay()->PollNextOverlayEvent( m_panel, &ev, sizeof ev ) )
		HandlePanelEvent( ev );
	while ( VROverlay()->PollNextOverlayEvent( m_thumb, &ev, sizeof ev ) )
		if ( ev.eventType == VREvent_OverlayClosed )
			s_quit = true;
	while ( VRSystem()->PollNextEvent( &ev, sizeof ev ) )
		HandleSystemEvent( ev );
}

// ---------------------------------------------------------------------------------------------------------------------
// Main loop

void App::Tick()
{
	const double now = Now();
	const double dt = m_lastTick > 0 ? std::min( now - m_lastTick, 0.1 ) : 0.0;
	m_lastTick = now;

	PollEvents();
	if ( s_showDashboard.exchange( false ) )
	{
		fprintf( stderr, "second launch (quick-launch tile): showing the dashboard tab\n" );
		ShowDashboardPanel();
	}
	if ( now >= m_nextUniverseCheck )
	{
		m_nextUniverseCheck = now + 0.25;
		CheckUniverse();
	}
	if ( m_rebaseAt >= 0 && now >= m_rebaseAt )
	{
		if ( m_rebaseDropsMove )
			Rebase();
		else
			Rebaseline();
	}
	if ( m_state == MoverState::WaitingForChaperone && now >= m_nextRetry )
	{
		m_nextRetry = now + 2.0;
		Rebase();
	}

	InputFrame in;
	// Polled every tick, whatever the state: SteamVR only reports an action's bindings once input has been polled.
	const bool haveInput = m_inputOk && m_actions.Update( in );
	if ( haveInput && MoveAllowed() )
	{
		Tracked hmd, hands[2];
		// Height/reset and idle input need no tracking IPC. A fresh pose is only needed for a grab or snap turn.
		if ( in.drag[0] || in.drag[1] || in.turn[0] || in.turn[1] || in.snapLeft || in.snapRight )
		{
			TrackedDevicePose_t poses[k_unMaxTrackedDeviceCount];
			VRSystem()->GetDeviceToAbsoluteTrackingPose( TrackingUniverseRawAndUncalibrated, 0.0f, poses, k_unMaxTrackedDeviceCount );
			hmd = ReadPose( k_unTrackedDeviceIndex_Hmd, poses );
			hands[0] = ReadPose( VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_LeftHand ), poses );
			hands[1] = ReadPose( VRSystem()->GetTrackedDeviceIndexForControllerRole( TrackedControllerRole_RightHand ), poses );
		}
		const bool enabled[2] = { m_settings.leftHand, m_settings.rightHand };

		// One hand at a time per gesture; a hand that already holds the world keeps it.
		auto pick = []( const bool held[2], bool active0, bool active1 ) {
			if ( active0 && held[0] )
				return 0;
			if ( active1 && held[1] )
				return 1;
			return held[0] ? 0 : held[1] ? 1 : -1;
		};
		// Double-press + hold (like OVR Advanced Settings): a plain press of a shared button never grabs the world.
		bool dragHeld[2], turnHeld[2];
		for ( int h = 0; h < 2; h++ )
		{
			const bool d = m_dragGate[h].Update( in.drag[h], now ), t = m_turnGate[h].Update( in.turn[h], now );
			dragHeld[h] = ( m_settings.doublePress ? d : in.drag[h] ) && enabled[h];
			turnHeld[h] = ( m_settings.doublePress ? t : in.turn[h] ) && enabled[h];
			if ( m_settings.haptics && m_inputOk && ( ( dragHeld[h] && !m_dragWasOn[h] ) || ( turnHeld[h] && !m_turnWasOn[h] ) ) )
				m_actions.Pulse( h == 0 ? Hand::Left : Hand::Right, 0.04f, 0.5f ); // "grabbed"
			m_dragWasOn[h] = dragHeld[h];
			m_turnWasOn[h] = turnHeld[h];
		}
		const int dragHand = pick( dragHeld, m_drag[0].Active(), m_drag[1].Active() );
		const int turnHand = pick( turnHeld, m_turn[0].Active(), m_turn[1].Active() );
		for ( int h = 0; h < 2; h++ )
		{
			m_drag[h].Update( h == dragHand, hands[h].valid, hands[h].pos, m_settings.dragMultiplier, m_settings.lockHeight, m_playspace );
			m_turn[h].Update( h == turnHand && hmd.valid, hands[h].valid, hands[h].pos, hmd.pos, m_playspace );
		}

		if ( hmd.valid )
		{
			const double snap = m_settings.snapDegrees * kDegToRad;
			if ( in.snapLeft )
			{
				m_playspace.RotateAbout( hmd.pos, -snap );
				Pulse( true );
			}
			if ( in.snapRight )
			{
				m_playspace.RotateAbout( hmd.pos, snap );
				Pulse( true );
			}
		}
		// "Raise me" moves the world down.
		if ( in.heightUp != in.heightDown )
			m_playspace.Translate( { 0, ( in.heightUp ? -1.0 : 1.0 ) * m_settings.heightSpeed * dt, 0 } );

		if ( in.reset )
		{
			if ( m_resetHeldSince < 0 )
			{
				m_resetHeldSince = now;
				m_resetFired = false;
			}
			else if ( !m_resetFired && now - m_resetHeldSince >= kResetHoldSeconds )
			{
				m_resetFired = true;
				Rebase();
				Pulse( true );
			}
		}
		else
			m_resetHeldSince = -1;
	}

	if ( MoveAllowed() )
	{
		ApplyIfChanged();
		if ( m_playspace.Moved() && m_rebaseAt < 0 && now >= m_nextExternalCheck )
		{
			m_nextExternalCheck = now + 0.25;
			if ( m_chaperone.ModifiedByOthers() )
			{
				// Not stuck for good: whatever the other program was doing, look at the saved playspace again shortly.
				fprintf( stderr, "another program changed the chaperone working copy; pausing for %.0f s\n", kResumeAfterPause );
				Pause( false );
				m_rebaseAt = now + kResumeAfterPause;
				m_rebaseDropsMove = true;
			}
			else
				CheckLivePreview( now );
		}
	}

	if ( m_inputOk && ( m_bindingsStale || now - m_lastBindingRefresh > 1.0 ) )
		RefreshBindings( now );
	if ( m_inputOk && m_pinBindingsAt >= 0 && now >= m_pinBindingsAt )
	{
		m_pinBindingsAt = -1;
		m_actions.PinUserBindings();
	}
	if ( m_panelDirty && now >= m_panelRetryAt && VROverlay()->IsActiveDashboardOverlay( m_panel ) )
		RedrawPanel();
}

int App::Run()
{
	while ( !s_quit )
	{
		Tick();
		std::this_thread::sleep_for( std::chrono::milliseconds( GestureActive() ? 8 : 16 ) );
	}
	return 0;
}

void App::ShowDashboardPanel()
{
	VROverlay()->ShowDashboard( kPanelKey );
}

// ---------------------------------------------------------------------------------------------------------------------
// Panel

void App::HandlePanelAction( PanelAction action, double value )
{
	using A = PanelAction;
	switch ( action )
	{
	case A::Reset:
		Rebase();
		return;
	case A::EditBindings:
		if ( !m_inputOk || !m_actions.OpenBindingEditor( kAppKey ) )
			fprintf( stderr, "could not open the SteamVR binding editor\n" );
		return;
	case A::Raise:
		if ( MoveAllowed() )
			m_playspace.Translate( { 0, -value, 0 } ); // raising me moves the world down
		return;
	case A::Turn:
	{
		if ( !MoveAllowed() )
			return;
		TrackedDevicePose_t poses[1];
		VRSystem()->GetDeviceToAbsoluteTrackingPose( TrackingUniverseRawAndUncalibrated, 0.0f, poses, 1 );
		const Tracked hmd = ReadPose( k_unTrackedDeviceIndex_Hmd, poses );
		if ( hmd.valid )
			m_playspace.RotateAbout( hmd.pos, value * kDegToRad );
		return;
	}
	default:
		break;
	}
	Settings s = m_settings;
	if ( !ApplyPanelSetting( action, value, s ) )
		return;
	if ( s.largeChaperone != m_settings.largeChaperone || s.disableChaperone != m_settings.disableChaperone )
		m_appliedAny = false; // write new bounds / chaperone setting on the next tick
	m_settings = Clamped( s );
	SaveSettings( m_settings );
}

// Re-reads what every action is bound to. Runs on its own (not only while the panel is visible) and as soon as SteamVR
// says the bindings were reloaded, so the panel is already right when it is opened.
void App::RefreshBindings( double now )
{
	m_lastBindingRefresh = now;
	m_bindingsStale = false;
	const auto fresh = m_actions.Bindings();
	if ( fresh == m_bindings )
		return;
	m_bindings = fresh;
	m_panelDirty = true;
	std::string line = "bindings:";
	for ( const auto &b : m_bindings )
		line += "  [" + b.first + " = " + b.second + "]";
	fprintf( stderr, "%s\n", line.c_str() );
}

void App::RedrawPanel()
{
	const double now = Now();
	m_panelDirty = false;
	if ( m_bindings.empty() && m_inputOk )
		RefreshBindings( now );
	PanelModel model;
	model.settings = m_settings;
	model.state = m_state;
	model.moved = m_playspace.Moved();
	model.offset = m_playspace.Offset();
	model.yawDegrees = m_playspace.YawDegrees();
	model.bindings = m_bindings;
	model.inputError = m_inputOk ? "" : m_inputError;
	model.controllersConnected = false;
	for ( ETrackedControllerRole role : { TrackedControllerRole_LeftHand, TrackedControllerRole_RightHand } )
	{
		const TrackedDeviceIndex_t idx = VRSystem()->GetTrackedDeviceIndexForControllerRole( role );
		if ( idx != k_unTrackedDeviceIndexInvalid && VRSystem()->IsTrackedDeviceConnected( idx ) )
			model.controllersConnected = true;
	}

	Canvas canvas( kPanelW, kPanelH );
	DrawPanel( canvas, m_buttons, model, m_hover );
	std::vector<uint8_t> px( (size_t)kPanelW * kPanelH * 4 );
	canvas.CopyRGBA( px.data() );
	if ( px == m_lastPanelPixels ) // nothing changed: sending identical pixels would still make SteamVR reload the texture
		return;
	std::string err;
	const bool ok = m_panelTex ? m_panelTex->Update( m_panel, px.data(), kPanelW, kPanelH, err )
							   : VROverlay()->SetOverlayRaw( m_panel, px.data(), kPanelW, kPanelH, 4 ) == VROverlayError_None;
	if ( !ok )
	{
		fprintf( stderr, "panel upload failed: %s\n", err.c_str() );
		m_panelDirty = true; // retry after the redraw interval even if no new UI event arrives
		m_panelRetryAt = now + 0.25;
		return;
	}
	if ( m_lastPanelPixels.empty() )
		fprintf( stderr, "panel: first upload via %s\n", m_panelTex ? "Vulkan texture (SetOverlayTexture)" : "SetOverlayRaw (flickers)" );
	m_lastPanelPixels = std::move( px );
}

// ---------------------------------------------------------------------------------------------------------------------
// Self test: exercises the real SteamVR round trip without needing controllers.

namespace
{
bool Near( const HmdMatrix34_t &a, const Mat34 &b, float eps = 1e-3f )
{
	for ( int r = 0; r < 3; r++ )
		for ( int c = 0; c < 4; c++ )
			if ( std::fabs( a.m[r][c] - (float)b.m[r][c] ) > eps )
				return false;
	return true;
}
} // namespace

int App::SelfTest()
{
	int failures = 0;
	auto check = [&]( bool ok, const char *what ) {
		printf( "%s  %s\n", ok ? "PASS" : "FAIL", what );
		if ( !ok )
			failures++;
	};
	IVRChaperoneSetup *cs = VRChaperoneSetup();

	if ( m_inputOk )
	{
		InputFrame in;
		check( m_actions.Update( in ), "SteamVR Input accepted the action manifest and action set" );
		for ( const auto &b : m_actions.Bindings() )
			printf( "     %-18s -> %s\n", b.first.c_str(), b.second.c_str() );
	}
	else
		check( false, ( "SteamVR Input: " + m_inputError ).c_str() );

	// While the headset is not tracking SteamVR runs the dummy universe, which has no saved playspace: once a preview has
	// been shown and hidden there, nothing can be reloaded until tracking resumes, and when it does, SteamVR stores whatever
	// zero pose is live as the new universe's origin, our test move included. So only test while tracking.
	if ( CurrentUniverse() == kDummyUniverse )
	{
		check( false, "the headset is tracking (it is in standby: put it on or wake it, then run the test again)" );
		return 1;
	}
	// Like the main loop, retry while SteamVR has no working copy to give (it reloads the saved one for a moment after a
	// preview is hidden or its owner exits).
	const auto rebaseUntilReady = [this] {
		Rebase();
		for ( int i = 0; i < 20 && m_state != MoverState::Ready; i++ )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
			Rebase();
		}
	};
	rebaseUntilReady();
	check( m_state == MoverState::Ready, "captured the saved playspace" );
	if ( m_state != MoverState::Ready )
		return 1;
	const Mat34 baseStanding = m_playspace.BaseStanding(), baseSeated = m_playspace.BaseSeated();
	// While the headset is not tracking, SteamVR runs a placeholder universe (4 wall quads, 1 x 1 m play area) that has
	// no saved copy: reloading it yields zeros, so "restored exactly" can only be asserted for a real universe.
	uint32_t quads = 0;
	cs->GetWorkingCollisionBoundsInfo( nullptr, &quads );
	float areaX = 0, areaZ = 0;
	cs->GetWorkingPlayAreaSize( &areaX, &areaZ );
	const bool placeholder = quads == 4 && std::fabs( areaX - 1.0f ) < 1e-3f && std::fabs( areaZ - 1.0f ) < 1e-3f;
	printf( "     saved standing origin raw (%.3f, %.3f, %.3f), play area %.2f x %.2f m, %u wall quads%s\n", baseStanding.m[0][3],
		baseStanding.m[1][3], baseStanding.m[2][3], areaX, areaZ, quads,
		placeholder ? "\n     NOTE: placeholder universe (headset not tracking): exact restore is not asserted, only that reset equals a fresh reload" : "" );
	HmdQuad_t rectBefore;
	const bool haveRect = VRChaperone()->GetPlayAreaRect( &rectBefore );

	// Move 0.5 m along +X and turn 30 degrees about a point 1 m ahead.
	m_playspace.Translate( { 0.5, 0, 0 } );
	m_playspace.RotateAbout( { 0, 0, -1 }, 30.0 * kDegToRad );
	ApplyIfChanged();
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
	HmdMatrix34_t workingStanding, workingSeated;
	cs->GetWorkingStandingZeroPoseToRawTrackingPose( &workingStanding );
	cs->GetWorkingSeatedZeroPoseToRawTrackingPose( &workingSeated );
	check( Near( workingStanding, m_playspace.Standing() ), "SteamVR holds the moved standing origin" );
	check( Near( workingSeated, m_playspace.Seated() ), "SteamVR holds the moved seated origin" );
	check( !m_chaperone.ModifiedByOthers(), "no false 'edited by another program' detection" );
	if ( m_settings.largeChaperone )
	{
		float moveX = 0, moveZ = 0;
		cs->GetWorkingPlayAreaSize( &moveX, &moveZ );
		uint32_t moveQuads = 0;
		cs->GetWorkingCollisionBoundsInfo( nullptr, &moveQuads );
		printf( "     while moved: play area %.1f x %.1f m, %u wall quads\n", moveX, moveZ, moveQuads );
		check( moveX >= 100.0f && moveZ >= 100.0f && moveQuads == 4, "the working copy holds the huge chaperone while moved" );
	}
	PollEvents(); // the real loop does this every tick: our own preview must not look like an outside change
	check( m_rebaseAt < 0 && m_state == MoverState::Ready && m_playspace.Moved(), "applying the preview does not trigger a rebase event" );
	if ( haveRect )
	{
		// The chaperone stays anchored to the real room, so in the moved universe's coordinates its play area shifts.
		HmdQuad_t rectMoved;
		const bool ok = VRChaperone()->GetPlayAreaRect( &rectMoved );
		const Vec3 o = m_playspace.Offset();
		printf( "     play area corner0 before (%.3f,%.3f) moved (%.3f,%.3f); world offset (%.2f,%.2f,%.2f)\n", rectBefore.vCorners[0].v[0],
			rectBefore.vCorners[0].v[2], rectMoved.vCorners[0].v[0], rectMoved.vCorners[0].v[2], o.x, o.y, o.z );
		check( ok && ( std::fabs( rectMoved.vCorners[0].v[0] - rectBefore.vCorners[0].v[0] ) > 0.01f ||
						 std::fabs( rectMoved.vCorners[0].v[2] - rectBefore.vCorners[0].v[2] ) > 0.01f ),
			"the live universe moved (preview is active)" );
	}

	rebaseUntilReady();
	std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );
	cs->GetWorkingStandingZeroPoseToRawTrackingPose( &workingStanding );
	cs->GetWorkingSeatedZeroPoseToRawTrackingPose( &workingSeated );
	check( !m_chaperone.PreviewShown(), "preview is off after reset" );
	check( !m_playspace.Moved(), "offsets cleared after reset" );
	check( Near( workingStanding, m_playspace.BaseStanding() ) && Near( workingSeated, m_playspace.BaseSeated() ),
		"after reset the working copy is the freshly reloaded saved copy" );
	if ( !placeholder )
	{
		check( Near( workingStanding, baseStanding ), "reset restored the standing origin exactly" );
		check( Near( workingSeated, baseSeated ), "reset restored the seated origin exactly" );
		HmdQuad_t rectAfter;
		bool same = haveRect && VRChaperone()->GetPlayAreaRect( &rectAfter );
		for ( int i = 0; same && i < 4; i++ )
			for ( int k = 0; k < 3; k++ )
				same = same && std::fabs( rectAfter.vCorners[i].v[k] - rectBefore.vCorners[i].v[k] ) < 1e-3f;
		check( same, "the live play area is back where it was" );
		float backX = 0, backZ = 0;
		uint32_t backQuads = 0;
		cs->GetWorkingPlayAreaSize( &backX, &backZ );
		cs->GetWorkingCollisionBoundsInfo( nullptr, &backQuads );
		check( std::fabs( backX - areaX ) < 1e-3f && std::fabs( backZ - areaZ ) < 1e-3f && backQuads == quads,
			"reset restored the saved play area size and wall bounds" );
	}
	printf( failures ? "SELFTEST FAILED\n" : "SELFTEST PASSED\n" );
	return failures ? 1 : 0;
}
