# Metroid Room Viewer

`metroid-rooms.html` — one self-contained page. Choose your own Metroid (USA) ROM and it rebuilds every room
from the ROM's room, structure and macro tables (the same steps as the game's SetupRoom / DrawStruct and LoadGFX),
then shows it with Metroid Dual's 3D view. No game data is in the page.

- World map: every room cell, coloured by area (the area of each cell is a best guess, grown from the cells each
  area's special-items table names). Click a cell, or walk with the arrow keys.
- Area / Room lists: any room number of any area, including ones that aren't on the map.
- Doors (bubbles in their real colours), power-ups (from the special-items table) and lava, item balls etc. as in the app.
- 2D / 3D (F2), camera presets, depth, back wall, backdrop of the area.s own rock, lantern (light follows the pointer), grid.

Files: `mroom.js` (decoder, also used by the tests), `viewer.src.html` (page), `build.py` (inlines mroom.js ->
metroid-rooms.html), `test.js` / `shot.py` (checks against emulator dumps / screenshots in headless Chromium).
Checked: decoded nametables, attributes, pattern tables and palettes match the emulator exactly in Brinstar,
Norfair, Kraid's lair, Tourian and Ridley's lair.
