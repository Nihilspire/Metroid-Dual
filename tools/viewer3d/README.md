# Metroid 3D prototype

`metroid3d.html` rebuilds recorded Metroid frames in 3D with WebGL. Open it in Chrome/Edge and drag in a `.mdrec`.
The page holds no game data; recordings are made from your own ROM by the desktop driver.

How it reads a frame (all straight from memory, no guessing from pixels):
- Scroll `$FC/$FD`, nametable select + pattern tables from PPUCTRL shadow `$FF`, sprites from OAM `$0200`,
  background from the PPU nametables (mirroring already applied), palette from `$3F00`.
- Background tiles below `$A0` are solid (Metroid's own rule) -> blocks with front/top/bottom/side faces
  (hidden faces between neighbours skipped). Other tiles -> a back wall. `$FF` is empty.
- Sprites -> cut-outs halfway into the block depth. HUD sprites (EN, digits, tanks, missile icon) -> flat overlay.
- Light: a soft light follows Samus (the palette-0 sprites).

Controls: 2D/3D (F2), camera presets, drag = orbit, wheel = zoom, double-click = reset, Space = play/pause,
arrows = step. Depth slider, back wall near/deep/off, edges fade/clip/show.

Make a recording:
    drive "Metroid (USA).nes" record_brinstar.txt     # script: load a state, recopen, rec N buttons..., recclose
    python make_mdrec.py brinstar.raw brinstar.mdrec
Script commands: `rec N [UDLRABTE]` records N frames holding those buttons (T = Start, E = Select).
