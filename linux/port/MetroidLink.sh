#!/bin/bash
# Metroid Link for ROCKNIX (Anbernic RG DS): Metroid runs in RetroArch (FCEUmm) on the top screen, the Link screen
# (map, status, password, save slots) on the bottom. Talks to RetroArch through its network commands (UDP 55355).
# Put "Metroid (USA).nes" in the metroidlink folder next to this script (or in /storage/roms/nes).
[ -f /etc/profile ] && . /etc/profile

GAMEDIR="$(cd "$(dirname "$0")" && pwd)/metroidlink"
cd "$GAMEDIR" || exit 1
LOG="$GAMEDIR/launch.txt"; : > "$LOG"

# --- the ROM
ROM=""
for f in "$GAMEDIR/Metroid (USA).nes" "$GAMEDIR"/*.nes "/storage/roms/nes/Metroid (USA).nes" /storage/roms/nes/Metroid*.nes; do
  [ -f "$f" ] && { ROM="$f"; break; }
done
[ -z "$ROM" ] && { echo "No Metroid ROM found (put it in $GAMEDIR)" | tee -a "$LOG"; exit 1; }

# --- RetroArch + the FCEUmm core (ROCKNIX keeps cores in a few places depending on the build)
RA="$(command -v retroarch || echo /usr/bin/retroarch)"
CORE=""
for c in /tmp/cores/fceumm_libretro.so /usr/lib/libretro/fceumm_libretro.so /storage/.config/retroarch/cores/fceumm_libretro.so \
         /usr/lib/aarch64-linux-gnu/libretro/fceumm_libretro.so; do
  [ -f "$c" ] && { CORE="$c"; break; }
done
[ -z "$CORE" ] && CORE="$(find /tmp/cores /usr/lib/libretro /storage/.config/retroarch/cores -name 'fceumm_libretro.so' 2>/dev/null | head -1)"
[ -z "$CORE" ] && { echo "FCEUmm core not found" | tee -a "$LOG"; exit 1; }
CFG=/storage/.config/retroarch/retroarch.cfg
CFGARG=(); [ -f "$CFG" ] && CFGARG=(--config "$CFG")
echo "ROM $ROM | core $CORE | retroarch $RA" >> "$LOG"

# --- the Link screen (bottom panel; ROCKNIX's sway config sends "... Bottom" windows to the second screen)
export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
export MD_TOP_OUTPUT="${MD_TOP_OUTPUT:-DSI-2}"
export MD_BOTTOM_OUTPUT="${MD_BOTTOM_OUTPUT:-DSI-1}"
export ML_ROM="$ROM"
chmod +x ./metroidlink
./metroidlink > "$GAMEDIR/stdout.txt" 2>&1 &
LINK=$!
sleep 1
swaymsg "focus output $MD_TOP_OUTPUT" >/dev/null 2>&1

# --- RetroArch with network commands on (only for this launch: --appendconfig doesn't change your settings)
"$RA" "${CFGARG[@]}" --appendconfig "$GAMEDIR/retroarch-link.cfg" -L "$CORE" "$ROM" >> "$LOG" 2>&1

kill "$LINK" 2>/dev/null
wait "$LINK" 2>/dev/null
