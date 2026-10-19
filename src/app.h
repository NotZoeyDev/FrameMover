#pragma once
#include "actions.h"
#include "chaperone.h"
#include "config.h"
#include "panel.h"
#include "playspace.h"
#include "ui.h"
#include "vk_texture.h"

#include <openvr.h>

#include <atomic>
#include <memory>
#include <string>

constexpr const char *kAppKey = "dev.framemover.app";
constexpr const char *kPanelKey = "dev.framemover.panel";

// Directory holding action_manifest.json: next to the executable (build tree), or the installed copy.
std::string FindDataDir();

class App
{
public:
	static std::atomic<bool> s_quit;
	static std::atomic<bool> s_showDashboard; // set by SIGUSR1: a second launch (quick-launch tile) asks us to show our tab

	App() = default;
	~App();
	bool Init( std::string &error );
	int Run();
	// Applies a known offset to the live playspace, verifies it through SteamVR, restores, verifies again.
	int SelfTest();
	void ShowDashboardPanel();

private:
	void Tick();
	void PollEvents();
	void HandleSystemEvent( const vr::VREvent_t &ev );
	void RefreshBindings( double now );
	void HandlePanelEvent( const vr::VREvent_t &ev );

	void Rebase();            // adopt SteamVR's current saved playspace as "original", dropping any move
	void Rebaseline();        // adopt it as the baseline but keep the move the user made on top of it
	void ScheduleRebase( bool dropMove ); // debounced Rebase()/Rebaseline() once a burst of events is over
	uint64_t CurrentUniverse() const;     // the tracking universe SteamVR is using, 0 if unknown
	void CheckUniverse();                 // a universe switch silently drops our preview and makes the captured playspace stale
	void ApplyIfChanged();
	void CheckLivePreview( double now ); // is SteamVR still using the playspace we applied? re-apply if it silently dropped it
	bool LiveStanding( Mat34 &out ) const;      // the standing zero pose SteamVR really uses, from the headset's poses
	void LogBaseline( const char *what ) const; // universe, captured base vs live, saved bounds
	void StartTrace( double now );              // log live vs ours at 20 Hz for a moment
	void Trace( double now );
	void Reanchor( double now );                // re-anchor the saved playspace to the current tracking map (see app.cpp)
	void Pause( bool restore );
	void Pulse( bool bothHands );

	struct Tracked
	{
		bool valid = false;
		Vec3 pos;
	};
	Tracked ReadPose( vr::TrackedDeviceIndex_t idx, const vr::TrackedDevicePose_t *poses ) const;

	void RedrawPanel();
	void HandlePanelAction( PanelAction action, double value );
	bool MoveAllowed() const { return m_state == MoverState::Ready; }
	bool GestureActive() const { return m_drag[0].Active() || m_drag[1].Active() || m_turn[0].Active() || m_turn[1].Active(); }

	bool m_vrInit = false;
	Settings m_settings;
	Chaperone m_chaperone;
	Playspace m_playspace;
	Actions m_actions;
	bool m_inputOk = false;
	std::string m_inputError;

	DragGesture m_drag[2];
	TurnGesture m_turn[2];
	DoublePressGate m_dragGate[2], m_turnGate[2]; // per hand: tap, then press and hold
	bool m_dragWasOn[2] = {}, m_turnWasOn[2] = {};   // previous gated state, to buzz when a grab starts
	MoverState m_state = MoverState::WaitingForChaperone;

	// timing (seconds on a steady clock)
	double m_lastTick = 0;
	double m_rebaseAt = -1;       // pending debounced rebase, or -1
	bool m_rebaseDropsMove = false; // some pending trigger means the move must go (new original), not just be re-based
	uint64_t m_universe = 0;      // the tracking universe seen last
	double m_nextUniverseCheck = 0;
	double m_pinBindingsAt = -1;  // when to re-pin the saved bindings after SteamVR reloaded them, or -1
	double m_nextRetry = 0;       // next attempt to get a playspace while waiting
	double m_nextExternalCheck = 0;
	double m_resetHeldSince = -1;
	bool m_resetFired = false;

	Vec3 m_appliedOffset;
	double m_appliedYaw = 0;
	bool m_appliedAny = false;
	std::vector<double> m_healTimes; // recent self-repairs, for the back-off
	double m_healBackoffUntil = 0;
	double m_lastApply = -10; // when we last wrote a preview; used to tell our own echo events from outside changes
	double m_traceStart = 0, m_traceUntil = -1, m_nextTrace = 0; // diagnostics trace window
	bool m_reanchorTried = false; // re-anchored once in this tracking world already
	double m_reanchoredAt = -10;  // when; commit events right after it are our own echo

	// dashboard panel
	vr::VROverlayHandle_t m_panel = vr::k_ulOverlayHandleInvalid;
	vr::VROverlayHandle_t m_thumb = vr::k_ulOverlayHandleInvalid;
	ButtonPanel m_buttons;
	bool m_panelDirty = true;
	double m_panelRetryAt = 0;
	double m_lastBindingRefresh = -10;
	bool m_bindingsStale = true;
	int m_hover = -1;
	std::vector<std::pair<std::string, std::string>> m_bindings;
	std::vector<uint8_t> m_lastPanelPixels; // what the compositor currently has
	std::unique_ptr<VulkanContext> m_vk;    // declared before the texture that lives on it
	std::unique_ptr<OverlayTexture> m_panelTex;
};
