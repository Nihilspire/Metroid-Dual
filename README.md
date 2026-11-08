# Metroid Dual

Metroid (NES) on two screens: the game on one, a **Metroid Link** companion screen on the other.
It shows live status, an auto-map with a next-objective guide, and save state shortcuts
Keeps track of items obtained in game as well as total playtime for ending tracking.
It runs as an Android app (Surface Duo, dual-screen handhelds, a second display, or split screen) and on
ROCKNIX dual-screen handhelds (Anbernic RG DS). There's also **Metroid Link**, the companion screen on its own,
for playing in RetroArch if you want to keep Retroachivement support.

**No game files are included.** You supply your own *Metroid (USA)* ROM. The font, sprites, map, rooms and item
table are read from it at runtime.

## AI-assisted?
Feel free to skip over if you feel strongly about it. Claude's `Opus 5.5` was used for ram mapping and initial coding and `Qwen-3-coding` was used for code review. Code is open-source, feel free to use as a base and human it up.

## Downloads
See [Releases](../../releases):

| File | What it is |
|---|---|
| `Metroid Dual <ver>.apk` | Android: emulator + Link screen in one app |
| `Metroid Dual <ver> ROCKNIX.zip` | ROCKNIX port (game on the top screen, Link on the bottom) |
| `Metroid Link <ver>.apk` | Android: Link screen only, reading Metroid running in RetroArch. Works with Retroachivements. |
| `Metroid Link <ver> ROCKNIX.zip` | ROCKNIX: starts RetroArch on the top screen with the Link screen on the bottom |

## Features
- **Link screen:**
  - Status: energy and tanks, missiles, gear, bosses, play time, and current ending.
  - Auto-map in the style of Zero Mission, coloured by area. The close-up view can be dragged; tap for the whole map.
    Shows item and beam markers and a NEXT guide to the next objective.
  - Mission report (gameplay stat screen) with a signature at the ending.
- **Save states:** 4 slots with screenshots, plus quick save/load and an auto state on exit.
- **Screens:**
  - Surface Duo hinge, a second display, split screen (side by side / top and bottom with a hinge gap),
    or one screen with a side panel or overlay.
  - 16:9 borders themed from the game's own tiles. Border based on the most populous tile in that area.
- **Controls:** Touch pad support and controller support with rebinding.
- **NO SLOWDOWN:** the emulator's overclock removes the original game's slowdown.
- **Launcher music:** synthesized menu music.
- **Experimental 3D view:** the game rebuilt in 3D every frame, with a light that follows Samus and a backdrop of the
  area's own textures.
- **Playtest codes:** hidden behind the Konami code on the launcher.
- **Room viewer:** `tools/roomviewer` is a standalone web page that browses every room in 2D/3D from your ROM.

## Source layout
| Path | |
|---|---|
| `app/` | Android app (Java, no AndroidX). `assets/menu` = launcher/pause menu, `assets/link` = Link screen (HTML/JS) |
| `native/` | libretro frontend for FCEUmm (`nescore.c`), JNI layer, desktop test driver (`drive.c`) |
| `linux/` | ROCKNIX/Linux build: SDL2 + QuickJS host that runs the same pages (`metroiddual.c`, `metroidlink.c`, `gl3d.c`) |
| `linkapp/` | Metroid Link for Android (WebView + RetroArch network commands) |
| `tools/` | build scripts, room viewer, 3D prototype, mock RetroArch server |
| `tests/` | page and password tests against RAM dumps (paths point at local dumps; not included) |

## Building
Android (no NDK needed; see `tools/setup_toolchain.sh`):
```
bash tools/setup_toolchain.sh      # bionic headers, stub libs, compiler-rt builtins, clones fceumm
bash tools/build_apk.sh            # -> out/Metroid Dual <ver>.apk
bash tools/build_link_apk.sh       # -> out/Metroid Link <ver>.apk
```
Needs clang, lld, llvm, aapt, zipalign, apksigner, dalvik-exchange, openjdk-17 and android.jar (API 33, e.g.
from github.com/Sable/android-platforms). The signing keystore is not in the repo: `build_apk.sh` creates a new
one if `tools/metroiddual.keystore` is missing. Builds signed with a different key can't update an installed copy.

ROCKNIX (aarch64, glibc 2.36) and desktop test builds:
```
make -C linux arm64 link-arm64     # cross-compiled with zig cc against SDL 2.26 headers + a stub libSDL2
make -C linux host link-host       # x86_64 with the system SDL2, for testing under Xvfb
```

## Credits and licences
This project is licensed under the **GNU General Public License v2.0 or later** (see `LICENSE`).
If you do use parts of it in your project, please credit me as a "playtester".
The release builds include:
- **libretro-fceumm** (GPL-2.0), github.com/libretro/libretro-fceumm, unmodified, commit `ac0d00a7`.
- **QuickJS-ng** (MIT), github.com/quickjs-ng/quickjs, unmodified, commit `c1ed1943` (ROCKNIX builds).
- **SDL 2** (zlib) headers 2.26.5. The ROCKNIX builds use the device's own SDL2 library.

RAM and ROM addresses are from nmikstas/metroid-disassembly, checked against the running game.
Metroid is a trademark of Nintendo. This is an unofficial fan project and is not affiliated with Nintendo.

## RAM / ROM used (Metroid USA)
`$1D` GameMode, `$1E` MainRoutine (3 = running, 5 = paused), `$4F/$50` MapPosY/X, `$74` InArea ($10-$14),
`$106/$107` health (BCD; full tanks = hi>>4), `$10A/$10B` escape timer (BCD, $FF = off), `$6877` TankCount,
`$6878` SamusGear (01 bombs, 02 high jump, 04 long beam, 08 screw, 10 maru mari, 20 varia, 40 wave, 80 ice),
`$6879/$687A` missiles/max, `$687B/$687C` Kraid/Ridley statue status, `$687D-$687F` Samus age (ticks every
256 frames, low byte wraps at $D0), `$6886` NumUniqueItems, `$6887-$68FC` unique item history, `$69B3` JustInBailey.
ROM (file offsets incl. header): font `0x1B4D0` (64 tiles), sprite table `0x18010`, world map `0x254E` (32x32),
item table `0x1039` (60 words: type<<10 | x<<5 | y), frame/placement tables `0x461B` / `0x46EF`.
Addresses from nmikstas/metroid-disassembly, verified by booting the ROM in the desktop driver.

