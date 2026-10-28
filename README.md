# FrameMover

An [OVR Advanced Settings](https://github.com/OpenVR-Advanced-Settings/OpenVR-AdvancedSettings)-style **playspace mover that
runs on the Steam Frame itself** (native Linux ARM64, against the SteamVR installed on the headset).

* **Grab and move** the world, **grab and turn** it around your head, **snap turn**, **raise / lower** yourself.
* **Key binds are SteamVR Input bindings**: rebind them in SteamVR's own binding editor.
* **Reset to original** puts back exactly the playspace you had saved.

## How it works

The mover shows your moved playspace as a live preview in SteamVR, the same way OVR Advanced Settings does. Your saved
playspace is not changed by moving, so Reset, quitting the mover or a crash always bring back the original.

* The virtual world moves; the chaperone stays where your real room is.
* **Huge chaperone while moved** (panel toggle, on by default): replaces the play area with a 200 x 200 m square while you are moved, so the chaperone doesn't pop up at the edge of your real play area. The chaperone is also your only warning before a real wall.
* **No chaperone** (panel toggle, off by default): also turns off SteamVR's chaperone while you are moved. The previous setting is put back on Reset, on quit and on the next start.
* After a fresh boot, SteamVR keeps forcing the saved floor height until the playspace is re-saved for the current tracking session (what pressing Accept in SteamVR's playspace setup does). The mover does this for you, once, the first time it is needed: it saves your playspace again unchanged, then puts your move back.
* A recenter or finished room setup becomes the new original. Losing tracking drops the move.
* A backup of your chaperone file is kept in `~/.local/share/frame-mover/backup/`.
* Moves are not remembered across restarts of the mover.

## Default controls (Steam Frame controllers)

| Action | Default binding | Notes |
|---|---|---|
| Grab and move | Bumper (either hand) | double-press and hold, move your hand: the world follows it |
| Grab and turn | Thumbstick click (either hand) | double-press and hold, swing the hand around your head: the world rotates about you |
| Snap turn left / right | Left D-pad left / right | angle in the panel (default 45 deg) |
| Raise me / Lower me | Left D-pad up / down | hold; speed in the panel |
| Reset to original | Left View button | hold 1.5 s, buzz when done |

**Double-press + hold**: grab and turn only start when you tap the button, release, and press it again within 0.4 s and keep
holding it, so a single press (which a game may use) never grabs the world. Switch it off in the panel to grab on a plain hold.
Buttons bound as *toggle* in SteamVR's editor can't be double-pressed, so turn the option off if you bind one that way.

Change bindings from the dashboard tab **FrameMover** > **Edit bindings in SteamVR**; the panel shows what each action is bound to.
Your game still sees those buttons too; to withhold them, set `overrideGameInput=1` in `~/.config/frame-mover/config.ini` and enable
SteamVR's developer setting "Experimental overlay input overrides".

The dashboard tab also has Reset, raise / lower / turn buttons, grab speed, snap angle, raise speed, lock height, per-hand enable and
haptics. Settings are stored in `~/.config/frame-mover/config.ini`.

## Language

The panel and the SteamVR binding names are available in English and Japanese. The language follows the Steam client's language,
then `$LC_ALL` / `$LANG`; English is used when the language has no translation. To pick one yourself, use the language menu next to
the panel's title (saved to `~/.config/frame-mover/lang`; "Automatic" goes back to detection), or set `FRAME_MOVER_LANG` (e.g. `ja`,
`en`), which wins at startup. Translations are a table in `src/translations.cpp`: add an entry there (code, Steam language name,
native name, font, strings) and a `localization` block in `data/action_manifest.json`; missing strings fall back to English.

## Quick-launch tile

`--install` adds FrameMover to SteamVR's quick-launch bar and the Frame's app launcher, and starts it with SteamVR. Pressing the
tile while it is already running opens its dashboard tab. `--uninstall` removes all of it.

## Build, install

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel && cmake --build build -j
ctest --test-dir build --output-on-failure      # tests, no SteamVR needed
./build/frame-mover --install         # copies to ~/.local, registers with SteamVR, autostarts with it
./build/frame-mover --uninstall
```

Needs `libopenvr_api.so` and headers from `/opt/steamvr` (override with `-DOPENVR_LIB=... -DOPENVR_HEADERS=...`), cairo and Vulkan.

To build only the tests, without graphics libraries or SteamVR:

```
cmake -S . -B build-tests -DFRAME_MOVER_BUILD_APP=OFF
cmake --build build-tests
ctest --test-dir build-tests --output-on-failure
```

Other commands: `--selftest` (moves the live playspace for about a second and checks it comes back; stop the running mover first,
and the headset must be awake and tracking), `--screenshot FILE.png` (renders the panel, no SteamVR), `--show-dashboard`.

### AppImage installer

`./packaging/appimage/build.sh` (on an aarch64 machine such as the Frame) builds `build/FrameMover-installer-<arch>.AppImage`:

```
./FrameMover-installer-aarch64.AppImage              # asks: Install / Uninstall
./FrameMover-installer-aarch64.AppImage install      # no dialog (also --install)
./FrameMover-installer-aarch64.AppImage uninstall    # no dialog (also --uninstall)
```

It needs FUSE (or `--appimage-extract-and-run`) and the same system libraries as the program. Have SteamVR running so the app
can be registered. Uninstall keeps `~/.config/frame-mover` and the chaperone backups.
