#pragma once
#include "playspace.h"

#include <openvr.h>
#include <string>
#include <vector>

// Thin wrapper over IVRChaperoneSetup. Moves only ever edit SteamVR's *working copy* and show it as a live preview; they
// are never committed, so "Restore" is exact: drop the preview and reload the saved copy. The one commit is Reanchor(),
// which writes the saved playspace back unchanged (see there).
class Chaperone
{
public:
	// Reloads the saved copy into the working copy and reads the zero poses the user had set. The preview is hidden meanwhile.
	// `keepSettingOverride` leaves SteamVR's chaperone setting as it is (the caller is about to Apply() again).
	bool Capture( Mat34 &standing, Mat34 &seated, bool keepSettingOverride = false );
	// Puts these zero poses on screen immediately (working copy + preview).
	// `largeChaperone` also replaces the working play area / wall bounds with a huge box (and puts the saved ones back
	// when it is false again). `disableChaperone` additionally sets SteamVR's `steamvr/disableChaperone` setting for as
	// long as the preview is shown; the previous value is kept on disk and put back by Restore() / RecoverSetting().
	bool Apply( const Mat34 &standing, const Mat34 &seated, bool largeChaperone, bool disableChaperone );
	// Drops the preview and commits the saved playspace unchanged, like Accept in SteamVR's playspace setup. After the
	// Frame relocalizes into a playspace saved against an older tracking map, SteamVR keeps forcing that playspace's floor
	// height onto the live standing origin; committing re-anchors it to the current map and that stops. The only commit
	// the mover ever makes: the content (walls, origins in the room) stays what the user saved.
	bool Reanchor();
	// Puts the chaperone setting back if an earlier run died while holding the override. Call once after VR_Init.
	static void RecoverSetting();
	// Drops the preview and reloads the saved copy.
	void Restore();
	// True when the working copy no longer holds what Apply() last wrote, i.e. another program (SteamVR room
	// setup, another overlay) is editing it.
	bool ModifiedByOthers() const;
	// True when the huge play area we wrote is no longer what SteamVR reports: a universe switch drops the whole preview,
	// bounds included, even when the zero pose happens to look right.
	bool BoundsDropped() const;
	bool PreviewShown() const { return m_applied; }
	// "play area 1.00 x 1.00 m, 4 wall quads": what Capture() found (a floor-only playspace has the 1 x 1 m placeholder).
	std::string SavedBoundsText() const;

private:
	bool m_applied = false;
	Mat34 m_lastStanding, m_lastSeated;
	bool m_boundsOverridden = false;
	float m_savedAreaX = 0, m_savedAreaZ = 0;
	std::vector<vr::HmdQuad_t> m_savedQuads; // the working bounds as Capture() found them
	bool m_settingOverridden = false;
	std::string m_settingBefore; // "absent", "0" or "1": what steamvr/disableChaperone was before we set it

	void SetChaperoneDisabled( bool disabled );
};
