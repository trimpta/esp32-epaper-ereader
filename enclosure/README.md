# Enclosure

`case.scad` is a parametric OpenSCAD case: a front bezel (screen cutout + button/USB-C
holes) and a back shell (PCB standoffs + a battery pocket behind the board). **Every
dimension in it is a placeholder** — see the comment block at the top of the file.

## Before printing anything

1. Pull the official PCB outline: Elecrow-RD's GitHub repo for this board has a `3D file`
   folder with `.stp` files. Open in FreeCAD (or any STEP-capable CAD tool), read the
   bounding box and the mounting-hole positions, and update `PCB_WIDTH`, `PCB_LENGTH`,
   and the hole positions in `case.scad` to match.
2. Measure or find the real button/rotary/USB-C positions from the same STEP file (or
   physically, once you have the board) and update the button-row and USB-C cutout
   placement — the ones in the file are evenly-spaced guesses, not real coordinates.
3. Pick your actual battery and update `BATTERY_W`/`BATTERY_L`/`BATTERY_H` — the default
   (20×32×6mm) is sized for a common 502030-class LiPo pouch cell, but confirm it
   actually fits behind the board once `PCB_LENGTH` is corrected (a bigger board leaves
   more room; check before committing to a cell).

## Prior art worth looking at

No published case combines "fits the 2.13" board" with "has a battery bay" — the two
closest references, neither directly usable:

- [Case for CrowPanel 2.13" Epaper (Printables)](https://www.printables.com/model/1566902-case-for-crowpanel-213-epaper/related) —
  built for this exact board, but a plain two-part protective shell (no stated battery
  bay). Worth comparing dimensions against once you've pulled the real ones from the
  STEP file — if it's accurate it may be less work to remix than to model from scratch.
- [Enclosure for CrowPanel ESP32 4.2" (Printables)](https://www.printables.com/model/1482686-enclosure-for-a-crowpanel-esp32-42inch-e-paper-dis/related) —
  wrong board (4.2" panel, much bigger PCB) but explicitly built to cover the
  switches/GPIO and hide a battery — worth opening as a layout reference for how the
  battery pocket and button cutouts are arranged, even though none of its dimensions
  transfer directly.

## Print settings (typical for a small snap/screw-together case)

0.2mm layer height, 3 perimeters, 15–20% infill, no supports needed if printed as
oriented in the file (both parts print flat, cutout-side up). PETG or PLA are both fine;
PETG if the case will sit somewhere warm.
