#!/bin/bash
# Metroid Dual for ROCKNIX (Anbernic RG DS): game on the top screen, Metroid Link screen on the bottom.
# Put "Metroid (USA).nes" in the metroiddual folder next to this script.
[ -f /etc/profile ] && . /etc/profile

GAMEDIR="$(cd "$(dirname "$0")" && pwd)/metroiddual"
cd "$GAMEDIR" || exit 1

# Controller mappings from PortMaster, when it is installed (SDL also has its own built-in list).
for d in /storage/roms/ports/PortMaster /roms/ports/PortMaster /storage/.config/PortMaster /opt/system/Tools/PortMaster; do
  if [ -f "$d/gamecontrollerdb.txt" ]; then export SDL_GAMECONTROLLERCONFIG_FILE="$d/gamecontrollerdb.txt"; break; fi
done

export SDL_VIDEODRIVER="${SDL_VIDEODRIVER:-wayland}"
export MD_MODEL="${QUIRK_DEVICE:-}"
# Panel names under sway on the RG DS: top DSI-2, bottom DSI-1. Override here for other dual-screen devices.
export MD_TOP_OUTPUT="${MD_TOP_OUTPUT:-DSI-2}"
export MD_BOTTOM_OUTPUT="${MD_BOTTOM_OUTPUT:-DSI-1}"

chmod +x ./metroiddual
./metroiddual > "$GAMEDIR/stdout.txt" 2>&1
