# FrameMover

An [OVR Advanced Settings](https://github.com/OpenVR-Advanced-Settings/OpenVR-AdvancedSettings)-style **playspace mover that
runs on the Steam Frame itself** (native Linux ARM64, against the SteamVR that is installed on the headset).

* **Grab and move** the world with a controller button, **grab and turn** it around your head, **snap turn**, **raise / lower** yourself.
* **Key binds are SteamVR Input bindings**: rebind them in SteamVR's own binding editor (button in the dashboard tab).
* **Reset to original** puts back exactly the playspace / chaperone you had saved. Moves are never committed to SteamVR's chaperone; the one write the mover makes is re-anchoring your saved playspace unchanged (see below).

## How it works

The mover edits SteamVR's *working copy* of the standing and seated zero poses (`IVRChaperoneSetup`) and shows it with
`ShowWorkingSetPreview`, like OVR-AS. Moves never go through `CommitWorkingCopy`.

* The virtual world moves; the chaperone stays where your real room is (the play area shifts opposite to the move).
* **Huge chaperone while moved** (panel toggle, on by default, `largeChaperone` in `config.ini`): while the world is moved, the working copy's play area is replaced by a 200 x 200 m square with four wall quads, so SteamVR's chaperone does not show up at the edge of your (or the 1 x 1 m placeholder) play area. It lives in the same working copy as the moved playspace: never committed, and Reset puts the saved size and walls back. Switch it off and the chaperone you saved is used as is. Note that the chaperone is also the only warning before a real wall.
* **No chaperone** (panel toggle, off by default, `disableChaperone` in `config.ini`): while the world is moved, also sets SteamVR's `steamvr/disableChaperone` setting, for when the Frame still switches to passthrough outside the play area. That setting is written to `steamvr.vrsettings` immediately (SteamVR has no temporary settings), so the previous value is first saved to `~/.config/frame-mover/chaperone-setting.restore`; Reset, Quit and the next start put it back.
* Both chaperone options only exist while the world is moved and the preview is on screen, so they end with a Reset, a recenter, a finished room setup or a new tracking world, and they are written again after a relocalization. What the Frame does with them (checked on the device): the compositor reacts to the preview (`VREvent_Compositor_ChaperoneBoundsShown` / `Hidden`, and `disableChaperone` takes effect at once), the Frame's own chaperone is a circle of N wall quads plus a play area of radius x sqrt(2) in `chaperone_info.vrchap`, and the headset reports `Prop_Hmd_ForceRoomViewOutsideChaperone_Bool` = true. Its own settings for this are under Advanced > Room and Tracking: Quick Calibrate (small / medium / large / gigantic / standing space, a real room setup), "Allow Chaperone Bubble in Immersive Apps" (`camera/AllowBubbleInImmersiveApps`) and "Chaperone System" (`steamvr/disableChaperone`).
* **Reset** = drop the preview and `RevertWorkingCopy`, which reloads `~/.config/openvr/config/chaperone_info.vrchap`. Moves are never committed to that file. SteamVR does rewrite it on its own, in the background (it adds and prunes placeholder universes while the headset is not tracking, re-saves the seated origin after recenters, and its `cv` driver re-reads it every minute while tracking); the backups in `~/.local/share/frame-mover/backup/` show this.
* **Re-anchoring after a fresh boot.** The Frame relocalizes into your saved playspace, which is stored against the tracking map it was set up in. As long as that map differs from the current one, SteamVR (`UpdatePersistentMapFromMapJumpHiding` in `vrserver`) writes the saved floor height into the live standing origin many times a second; x, z and yaw stay ours, so moves work except for height, which snaps back to the floor. Accept in SteamVR's playspace setup stops this because it re-stores the playspace against the current map. The mover does the same the first time it sees the height pinned (x/z/yaw ours, only y replaced): it backs up `chaperone_info.vrchap`, hides the move, reloads the saved playspace and commits it unchanged, then puts the move back (log: `re-anchoring it to the current map`). Same walls, same origin in the room; at most once per tracking world. The commit's own events (RoomSetupCommitted, zero pose resets) within 3 s are not taken as a recenter.
* Crash or kill: SteamVR notices the preview owner is gone and reloads the saved chaperone itself (`vrserver` log: "Live chaperone preview process ... isn't running anymore, reloading saved chaperone").
* Extra safety net: a copy of `chaperone_info.vrchap` is kept in `~/.local/share/frame-mover/backup/` at every start and install (one per distinct content, newest 10).
* Grabbing uses controller positions in *raw* tracking space, so the move does not feed back into the measurement. Both the standing and the seated universe move together.
* **Tracking universe switches** (the Frame switches universe when tracking relocalizes against a saved map, and gives every new tracking world a placeholder universe): SteamVR silently drops the live preview when that happens, and the playspace the mover captured is stale. The mover reads the universe id (`Prop_CurrentUniverseId_Uint64`) several times a second, captures the new saved playspace and puts your move back on top of it, so a relocalization no longer undoes a move. Losing tracking (standby, the dummy universe 424242) and starting a new tracking world is different: raw tracking space is not the same any more, so the move is dropped. Recenter and finished room setup also make the new playspace the original and drop the move. SteamVR room setup in progress: the mover steps aside and puts everything back first; it looks at the saved playspace again when setup reports back, or after 90 s if it never does (a cancelled setup sends no event). The same applies after another program edits the working copy: the mover pauses for 3 s, not for good.
* Quitting the mover (dashboard Close, SIGTERM, SteamVR shutdown) restores the original.
* Moves are not remembered across restarts of the mover.

## Default controls (Steam Frame controllers)

| Action | Default binding | Notes |
|---|---|---|
| Grab and move | Bumper (either hand) | double-press and hold, move your hand: the world follows it |
| Grab and turn | Thumbstick click (either hand) | double-press and hold, swing the hand around your head: the world rotates about you |
| Snap turn left / right | Left D-pad left / right | angle in the panel (default 45 deg) |
| Raise me / Lower me | Left D-pad up / down | hold; speed in the panel |
| Reset to original | Left View button | hold 1.5 s, buzz when done |

**Double-press + hold** (like OVR Advanced Settings): grab and turn only start when you tap the button, release, and press it again within 0.4 s and keep holding it. A single press, which a game may use the same button for, never grabs the world. The grab ends when you let go, and you feel a short buzz when it starts. Switch it off in the panel ("Double-press + hold to grab") to grab on a plain hold. A button bound as *toggle* in SteamVR's editor can not be double-pressed, so turn the option off if you bind one of them that way.

Change any of them: dashboard tab **FrameMover** > **Edit bindings in SteamVR**. The panel shows what each action is
currently bound to and updates as soon as SteamVR reloads the bindings (it listens for SteamVR's binding events). SteamVR loads
bindings per controller type, so until a controller has connected in the current SteamVR session it has nothing to report: if you
have not saved bindings of your own, the panel then shows the defaults from `data/bindings/frame_controller.json` (which is what will
apply); if you have, it shows "not bound" (the panel says so).

**Bindings that reset after restarting SteamVR:** SteamVR remembers bindings saved from its editor as a Steam Workshop item
(`vr-input-workshop://<id>`, setting `frame_controller_250820_CurrentURL_steamvrinput` in the app's section of `steamvr.vrsettings`). When the controllers connect before
Steam has logged in (right after the headset boots), `vrserver` logs "Tried to load url that was not a file path while steam is not available or the user is not logged in ... Silently falling back
to the default binding" and you get the shipped defaults for the rest of the session. The saved item is already on disk
(`~/.local/share/Steam/steamapps/workshop/content/250820/<id>/*_legacy.bin`), so at every start, and again after SteamVR reloads bindings (for example after you save in the editor),
the mover copies it to `~/.config/frame-mover/user-bindings/frame_controller.json` and points that setting at the copy (`file://...`). Loading a file does not need Steam.
By default your game still sees those buttons too; to withhold them from the game set
`overrideGameInput=1` in `~/.config/frame-mover/config.ini` and enable SteamVR's developer setting
"Experimental overlay input overrides".

The dashboard tab also has: **Reset to original playspace**, raise / lower / turn buttons, grab speed, snap angle, raise speed,
lock height, per-hand enable, haptics. Settings are in `~/.config/frame-mover/config.ini`.

## Quick-launch tile

`--install` registers the app as a dashboard overlay with `show_overlay_in_quicklaunch`, so it appears in SteamVR's quick-launch
bar (artwork: `header.png`, `capsule.png`, generated at install time). SteamVR orders that bar by most recently launched, so
the tile can sit in the second row until it has been used. Pressing the tile while the mover is already running starts a second
copy, which tells the running one to open its dashboard tab (the pid is kept in `$XDG_RUNTIME_DIR/frame-mover.<uid>.lock`)
and exits. `--install` also writes `~/.local/share/applications/frame-mover.desktop` (the Frame's launcher lists desktop entries) and
`--uninstall` removes it.

## Build, install

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel && cmake --build build -j
ctest --test-dir build --output-on-failure      # pure tests, no SteamVR needed
./build/frame-mover --install         # copies to ~/.local, registers with SteamVR, autostarts with it
./build/frame-mover --uninstall
```

Needs `libopenvr_api.so` and headers from `/opt/steamvr` (override with `-DOPENVR_LIB=... -DOPENVR_HEADERS=...`), cairo and Vulkan.
`MinSizeRel` is the default: size optimization, link-time optimization when supported, unused-section removal and stripped symbols.
Use `-DCMAKE_BUILD_TYPE=Debug` for debugging. The installed app only needs the executable, bindings and generated artwork; source,
tests, compiler and build directories are not installed. Cairo, Vulkan and OpenVR remain shared system dependencies.

To run the pure model, binding, pixel and panel-command tests without graphics libraries or SteamVR:

```
cmake -S . -B build-tests -DFRAME_MOVER_BUILD_APP=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Other commands: `--selftest` (moves the live playspace for about a second through SteamVR, verifies, restores, verifies; with the
headset on this briefly shifts your world; stop the running mover first, it owns the dashboard tab, and the headset must be tracking: in standby SteamVR has no saved playspace to restore and would store the test move as the next universe's origin, so the test refuses to run), `--screenshot FILE.png` (renders the panel, no SteamVR), `--show-dashboard`.

### AppImage installer

`./packaging/appimage/build.sh` (run it on an aarch64 machine such as the Frame; it refuses anywhere else) builds `build/FrameMover-installer-<arch>.AppImage` (downloads `appimagetool` into `build/` on first use,
unless it is already on PATH). It carries the program and its data and does what `--install` / `--uninstall` do, nothing more:

```
./FrameMover-installer-aarch64.AppImage              # asks: Install / Uninstall (kdialog or zenity, else the terminal)
./FrameMover-installer-aarch64.AppImage install      # no dialog (also --install)
./FrameMover-installer-aarch64.AppImage uninstall    # no dialog (also --uninstall)
```

It needs FUSE or `--appimage-extract-and-run`, plus the same system libraries as the program itself (cairo, Vulkan, `libopenvr_api.so`
from `/opt/steamvr`). Have SteamVR running so the app can be (un)registered; otherwise the files are still installed or removed and the
output says what is left. Uninstall keeps `~/.config/frame-mover` and the chaperone backups.

## Log

What the running mover does is written to `~/.local/share/frame-mover/frame-mover.log` (timestamps, universe switches, rebases, re-applies, binding pins; one older generation kept as `.1`).
SteamVR starts the mover with stderr connected to nothing, so this file is the only record. `--selftest` prints to the terminal instead.
Every capture logs the universe, the captured origin next to the one SteamVR really uses, and the saved play area (a floor-only playspace has the 1 x 1 m placeholder with 4 wall quads).
When the preview goes up, and after every re-apply or re-anchor, `trace` lines record for 1.5 s, 20 times a second, the origin SteamVR uses next to ours and the difference per axis.

## Playspace snapping back on its own

SteamVR can still replace the live playspace with the saved one and the floor height returns to the original. What is known:

* `CheckLivePreview` compares the zero pose SteamVR is really using (recovered from the headset's pose in raw space and in the standing
  universe) with the one the mover applied. On a mismatch while idle it logs one line (`live playspace differs from ours ...` with the three
  origins and whether the working copy is still ours), then re-applies (`re-applied the playspace SteamVR had dropped`). It stops trying for 30 s
  if SteamVR replaces it three times in 20 s.
* A universe switch drops the whole preview, zero pose and huge play area included (measured: a 200 x 200 m preview becomes 1 x 1 m again when the universe id changes). The mover now notices through the universe id and re-applies on top of the new saved playspace; the log line is `tracking universe A -> B`.
* `HideWorkingSetPreview` is asynchronous: a `RevertWorkingCopy` right behind it can read the preview back as the "saved" playspace. `Capture` therefore reverts until the working copy no longer shows what the mover wrote.
* **Height pinned after relocalization (re-anchored, see How it works).** Trace of 2026-10-19 18:20:41, playspace `9095940541091324616` with 20 wall quads: during a drag the live height alternated between ours and exactly `-1.726` (the saved floor) every 50-100 ms while x/z/yaw stayed ours; when idle `-1.726` stuck. None of: writing again; hiding and showing; hiding, reloading and showing kept the height (the last one even left SteamVR on the saved origin without the map transform, `(-0.407 -1.600 -0.271 yaw -48.6)`). Moves held in fresh placeholder universes from -4 m to +3 m of height.
* SteamVR stores the live zero pose as the origin of a **new** universe it creates. If the preview were still up at that moment, the move would end up in `chaperone_info.vrchap` as the saved origin. The mover hides the preview within about 0.5 s of tracking being lost (dummy universe), before a new tracking world can start; `--selftest` refuses to run in standby for this reason.
* Other candidates seen in `vrserver.txt`: the `cv` driver's once-a-minute "Preserving worlds ..." followed by "Read chaperone JSON", recenter (system-button long press), and the
  Frame's own room-setup start/cancel (`ERoomSetupStep_RoomSetupFull`). A recenter or room-setup event is, by design, taken as a new original, which drops the move.
