METROID LINK for ROCKNIX (Anbernic RG DS)

Metroid runs in RetroArch (FCEUmm core) on the top screen; the Link screen runs on the bottom screen:
status, auto-map with next-location guide, live password, 4 save-state slots (RetroArch's slots 1-4), and a
button that opens the RetroArch menu. Nothing from the game is included.

INSTALL
  1. Copy MetroidLink.sh and the metroidlink folder into your ports folder (/storage/roms/ports).
  2. Put your "Metroid (USA).nes" in the metroidlink folder (or keep it in /storage/roms/nes).
  3. Start "MetroidLink" from Ports. Quitting RetroArch closes the Link screen too.

The launcher turns on RetroArch's network commands for this session only (retroarch-link.cfg is appended to
your config; your settings file isn't changed). The Link screen reads the game's memory with READ_CORE_MEMORY on
127.0.0.1:55355. To use another address/port, set ML_HOST / ML_PORT in MetroidLink.sh and network_cmd_port in
retroarch-link.cfg.

FILES
  metroidlink          the program (aarch64)
  assets/              the Link page (same as Metroid Dual's)
  link-storage.json    map progress, saved passwords, options
  slots.txt            when each slot was last saved from the Link screen
  log.txt, stdout.txt, launch.txt   logs
