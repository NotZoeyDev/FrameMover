#include "chaperone.h"

#include "config.h"
#include "files.h"
#include "vr_math.h"

#include <openvr.h>

#include <cstdio>
#include <array>
#include <chrono>
#include <thread>
#include <string>
#include <unistd.h>

using namespace vr;

namespace
{
// What the working copy reports after we wrote `want` (float precision on the wire).
bool SameAsWritten( const HmdMatrix34_t &got, const Mat34 &want )
{
	const auto w = MatrixCast<HmdMatrix34_t>( want );
	for ( int r = 0; r < 3; r++ )
		for ( int c = 0; c < 4; c++ )
			if ( std::fabs( got.m[r][c] - w.m[r][c] ) > 1e-4f * ( 1.0f + std::fabs( w.m[r][c] ) ) )
				return false;
	return true;
}

// Half the side of the huge play area, in meters. Far beyond anything a user can move the world, so SteamVR's
// proximity warning never gets near a wall.
constexpr float kHugeHalfSide = 100.0f;
constexpr float kWallHeight = 2.43f; // what SteamVR's own room setup uses

// Four walls of a square around the zero pose, wound the way SteamVR writes them (bottom, top, top, bottom per wall).
std::array<HmdQuad_t, 4> HugeWalls()
{
	const float h = kHugeHalfSide;
	const float xs[5] = { -h, h, h, -h, -h }, zs[5] = { h, h, -h, -h, h };
	std::array<HmdQuad_t, 4> quads;
	for ( int i = 0; i < 4; i++ )
	{
		const float ax = xs[i], az = zs[i], bx = xs[i + 1], bz = zs[i + 1];
		quads[i].vCorners[0] = { { ax, 0, az } };
		quads[i].vCorners[1] = { { ax, kWallHeight, az } };
		quads[i].vCorners[2] = { { bx, kWallHeight, bz } };
		quads[i].vCorners[3] = { { bx, 0, bz } };
	}
	return quads;
}
} // namespace

namespace
{
constexpr const char *kSteamVrSection = "steamvr";
constexpr const char *kDisableKey = "disableChaperone";

// SteamVR keeps settings in steamvr.vrsettings the moment they are written, so a crash or a SteamVR shutdown while we
// hold the override would leave the chaperone off for good. The value to put back is therefore saved on disk first.
std::string RestoreFilePath()
{
	return ConfigDir() + "/chaperone-setting.restore";
}

void PutDisableSetting( const std::string &value )
{
	IVRSettings *st = VRSettings();
	if ( !st )
		return;
	EVRSettingsError err;
	if ( value == "absent" )
		st->RemoveKeyInSection( kSteamVrSection, kDisableKey, &err );
	else
		st->SetBool( kSteamVrSection, kDisableKey, value == "1", &err );
}
} // namespace

void Chaperone::RecoverSetting()
{
	std::string value;
	if ( !files::ReadFile( RestoreFilePath(), value ) )
		return;
	const char *space = " \t\r\n\f\v";
	const size_t start = value.find_first_not_of( space );
	if ( start == std::string::npos )
		return;
	value = value.substr( start, value.find_first_of( space, start ) - start );
	if ( value == "absent" || value == "0" || value == "1" )
	{
		PutDisableSetting( value );
		fprintf( stderr, "put the SteamVR chaperone setting back after an earlier run did not exit cleanly\n" );
	}
	unlink( RestoreFilePath().c_str() );
}

void Chaperone::SetChaperoneDisabled( bool disabled )
{
	IVRSettings *st = VRSettings();
	if ( !st )
		return;
	if ( disabled && !m_settingOverridden )
	{
		EVRSettingsError err;
		const bool prev = st->GetBool( kSteamVrSection, kDisableKey, &err );
		const std::string before = err != VRSettingsError_None ? "absent" : ( prev ? "1" : "0" );
		files::MakeDirs( ConfigDir() );
		if ( !files::WriteFile( RestoreFilePath(), before + "\n" ) )
			return; // without a way back we do not touch the setting
		m_settingBefore = before;
		PutDisableSetting( "1" );
		m_settingOverridden = true;
	}
	else if ( !disabled && m_settingOverridden )
	{
		PutDisableSetting( m_settingBefore );
		unlink( RestoreFilePath().c_str() );
		m_settingOverridden = false;
	}
}

bool Chaperone::Capture( Mat34 &standing, Mat34 &seated, bool keepSettingOverride )
{
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	if ( !cs )
		return false;
	if ( m_applied )
	{
		cs->HideWorkingSetPreview();
		// Hiding is processed asynchronously: a Revert right behind it can still read our own preview back as the "saved"
		// playspace (zero poses and huge bounds included), and while SteamVR reloads the saved copy the working copy cannot
		// be read at all (about 10 ms in a real universe, a second or more in the dummy one while the headset is not
		// tracking). Wait until it reads back as something other than what we wrote; the caller retries after that.
		for ( int i = 0; i < 100; i++ )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 15 ) );
			cs->RevertWorkingCopy();
			HmdMatrix34_t st;
			if ( cs->GetWorkingStandingZeroPoseToRawTrackingPose( &st ) && !SameAsWritten( st, m_lastStanding ) )
				break;
		}
	}
	if ( !keepSettingOverride )
		SetChaperoneDisabled( false );
	m_applied = false;
	cs->RevertWorkingCopy();
	HmdMatrix34_t st, se;
	if ( !cs->GetWorkingStandingZeroPoseToRawTrackingPose( &st ) || !cs->GetWorkingSeatedZeroPoseToRawTrackingPose( &se ) )
		return false;
	standing = MatrixCast<Mat34>( st );
	seated = MatrixCast<Mat34>( se );
	m_boundsOverridden = false;
	uint32_t quads = 0;
	m_savedQuads.clear();
	// Asked for the count only (null buffer), SteamVR fills it in but returns false, so only the count says whether there are bounds.
	cs->GetWorkingCollisionBoundsInfo( nullptr, &quads );
	if ( quads > 0 )
	{
		m_savedQuads.resize( quads );
		if ( !cs->GetWorkingCollisionBoundsInfo( m_savedQuads.data(), &quads ) )
			m_savedQuads.clear();
		else
			m_savedQuads.resize( quads );
	}
	m_savedAreaX = m_savedAreaZ = 0;
	cs->GetWorkingPlayAreaSize( &m_savedAreaX, &m_savedAreaZ );
	return true;
}

// The editor's Accept, without changes: the saved playspace (walls and origins exactly where they are in the room) is
// committed again, which makes SteamVR store it relative to the tracking map it uses now.
bool Chaperone::Reanchor()
{
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	Mat34 standing, seated;
	if ( !cs || !Capture( standing, seated, true ) )
		return false;
	return cs->CommitWorkingCopy( EChaperoneConfigFile_Live );
}

bool Chaperone::Apply( const Mat34 &standing, const Mat34 &seated, bool largeChaperone, bool disableChaperone )
{
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	if ( !cs )
		return false;
	const auto st = MatrixCast<HmdMatrix34_t>( standing ), se = MatrixCast<HmdMatrix34_t>( seated );
	cs->SetWorkingStandingZeroPoseToRawTrackingPose( &st );
	cs->SetWorkingSeatedZeroPoseToRawTrackingPose( &se );
	if ( largeChaperone )
	{
		static auto walls = HugeWalls(); // OpenVR accepts a mutable pointer but only reads these fixed quads.
		cs->SetWorkingPlayAreaSize( 2 * kHugeHalfSide, 2 * kHugeHalfSide );
		cs->SetWorkingCollisionBoundsInfo( walls.data(), (uint32_t)walls.size() );
		m_boundsOverridden = true;
	}
	else if ( m_boundsOverridden )
	{
		cs->SetWorkingPlayAreaSize( m_savedAreaX, m_savedAreaZ );
		cs->SetWorkingCollisionBoundsInfo( m_savedQuads.data(), (uint32_t)m_savedQuads.size() );
		m_boundsOverridden = false;
	}
	SetChaperoneDisabled( disableChaperone );
	cs->ShowWorkingSetPreview();
	m_applied = true;
	m_lastStanding = standing;
	m_lastSeated = seated;
	return true;
}

void Chaperone::Restore()
{
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	if ( !cs )
		return;
	cs->HideWorkingSetPreview();
	SetChaperoneDisabled( false );
	cs->RevertWorkingCopy();
	if ( m_boundsOverridden && !m_savedQuads.empty() )
	{
		// A universe without a saved copy (the placeholder one) keeps whatever the working copy holds after a revert.
		cs->SetWorkingPlayAreaSize( m_savedAreaX, m_savedAreaZ );
		cs->SetWorkingCollisionBoundsInfo( m_savedQuads.data(), (uint32_t)m_savedQuads.size() );
	}
	m_applied = false;
	m_boundsOverridden = false;
}

bool Chaperone::ModifiedByOthers() const
{
	if ( !m_applied )
		return false;
	IVRChaperoneSetup *cs = VRChaperoneSetup();
	if ( !cs )
		return false;
	HmdMatrix34_t st, se;
	if ( !cs->GetWorkingStandingZeroPoseToRawTrackingPose( &st ) || !cs->GetWorkingSeatedZeroPoseToRawTrackingPose( &se ) )
		return false;
	return !SameAsWritten( st, m_lastStanding ) || !SameAsWritten( se, m_lastSeated );
}

bool Chaperone::BoundsDropped() const
{
	if ( !m_applied || !m_boundsOverridden )
		return false;
	IVRChaperone *c = VRChaperone();
	float x = 0, z = 0;
	if ( !c || !c->GetPlayAreaSize( &x, &z ) )
		return false; // no answer is not proof that it was dropped
	return std::fabs( x - 2 * kHugeHalfSide ) > 1.0f || std::fabs( z - 2 * kHugeHalfSide ) > 1.0f;
}
