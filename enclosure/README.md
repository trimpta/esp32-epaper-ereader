# Enclosure

`case.scad` is a parametric OpenSCAD case, built around the real connector layout read
off `docs/images/hardware-overview.webp` (the annotated board photo):

- **Left wall** (top to bottom): MENU button, Rotary Switch (encoder + knob), EXIT
  button — all three side-actuated, cut into `back_shell()`'s left wall in that order.
- **Bottom wall**: a USB-C slot only. UART0 and BAT deliberately have **no** exterior
  cutout — see the comment above `USBC_W`/`USBC_H` in `case.scad` for the reasoning
  (UART0 is debug-only; BAT is a JST pigtail meant to be connected before the case is
  closed). Revisit this if you want battery hot-swap without opening the case.
- **Top margin of the front bezel**: a single small RESET pinhole (paperclip-sized).
  BOOT and GPIO_D get no dedicated access — both are one-time flashing/debug
  interfaces, and opening the case is a reasonable ask for that.
- **Right edge**: nothing — the photo shows no components there.

Two printed pieces:

- `front_frame()` — the bezel over the display. Owns the screen cutout and the RESET
  pinhole (both accessed straight through the board face).
- `back_shell()` — the piece with actual wall height. Owns the MENU/ROTARY/EXIT cutouts
  and the USB-C slot (all accessed from the edge, through the wall), the PCB standoffs,
  and a flat, unwalled cavity behind the board for the battery to sit loose in.

Plus `button_extender()` — a small printable plunger for MENU and EXIT. The PCB sits
inset from the case's outer surface by roughly `WALL + CLEARANCE`, so a side-mounted
tactile switch's cap will not reach the case exterior on its own. Print two of these
(head-down on the bed, no supports needed), and friction-fit one into each slot from the
inside before closing the case — the head is wider than the slot so it can't fall out
either direction, and no glue is needed for retention (a drop can help if the shaft ends
up too short to reach the switch; see the comment block above `EXTENDER_HEAD_W` in
`case.scad` for the full assembly notes). The rotary knob does not get an extender —
encoder shafts/knobs typically already protrude further than a tactile button's cap, and
its cutout is sized to let the knob reach the exterior directly.

**Every dimension is still a placeholder** — see the `TODO(verify)` comment block at the
top of `case.scad`. The edge each connector lives on should now be correct (read from
the product photo), but exact positions along each edge, exact button/rotary/USB-C
sizes, and the board outline itself are still guesses, not measurements.

## Before printing anything

1. Pull the official PCB outline: Elecrow-RD's GitHub repo for this board has a `3D file`
   folder with `.stp` files. Open in FreeCAD (or any STEP-capable CAD tool), read the
   bounding box and the mounting-hole positions, and update `PCB_WIDTH`, `PCB_LENGTH`,
   and the hole positions in `case.scad` to match.
2. Measure or find the real MENU/ROTARY/EXIT/USB-C/RESET positions from the same STEP
   file (or physically, once you have the board) and update `MENU_Y`/`ROTARY_Y`/`EXIT_Y`,
   the USB-C position, and the RESET pinhole position — the ones in the file are
   evenly-spaced guesses along the correct edges, not real coordinates.
3. Pick your actual battery and confirm it fits within `BATTERY_CAVITY_H` (default 6mm)
   once `PCB_LENGTH`/`STANDOFF_H` are corrected — there's no dedicated pocket anymore,
   just flat depth behind the standoffs, so "does it fit" is now purely a depth check.
4. Once you know the real switch heights/positions, double check `EDGE_COMPONENT_Z` (the
   guessed Z height of the left/bottom wall cutouts) and `EXTENDER_SHAFT_LEN` (the
   guessed extender length) — both currently derived from placeholder numbers.

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
oriented in the file (both case halves print flat, cutout-side up; the button extenders
print flat, head-down). PETG or PLA are both fine; PETG if the case will sit somewhere
warm.
