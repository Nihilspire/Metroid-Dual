METROID DUAL 0.45 for ROCKNIX (Anbernic RG DS)
================================================

Install
  1. Copy MetroidDual.sh and the metroiddual folder into the "ports" folder of your games card
     (on the device: /storage/roms/ports/).
  2. Put your own Metroid (USA) ROM in the metroiddual folder, named "Metroid (USA).nes"
     (any .nes file in that folder also works). Nothing from the game ships with this port.
  3. In EmulationStation: Ports > MetroidDual (refresh the game list if it doesn't show yet).

Screens
  Top screen     the game, plus the launcher and pause menu (touch or D-pad).
  Bottom screen  the Link screen: status, auto-map with next-location guide, save-state slots, options.
  Touching the bottom screen never takes the controller away from the game.

Default controls (change them in SETTINGS > CONTROLLER BUTTONS)
  D-pad / left stick  move             A  jump (NES A)          B  shoot (NES B)
  X / Y               turbo jump / turbo shoot          SELECT / START  NES select / start
  L1 or SELECT+START  pause menu       R1  fast forward (hold)
  L2 / R2             quick save / quick load
  R3                  3D view on/off   (3D DEPTH + / - can be bound in CONTROLLER BUTTONS)
  The game is saved automatically ("CONTINUE") when you quit or close the port.

Files in the metroiddual folder
  states/        save states (slot1-4, quick, auto)
  settings.txt   settings and button mapping
  log.txt        log of the last run (send it if something goes wrong)

3D view (experimental): PAUSE > DEV SETTINGS > 3D VIEW, the bottom screen's OPTIONS tab, or R3.
Camera, block depth, back wall, backdrop and light are in the same places. Turn on SHOW FPS
(SETTINGS) to see the frame rate; the 3D view adds "3D" to that line.
If the GPU can't run it, 3D VIEW is hidden and the game draws as normal.
If the Link screen opens on the wrong panel, edit MD_TOP_OUTPUT / MD_BOTTOM_OUTPUT in MetroidDual.sh.
