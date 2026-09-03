# Enclosure

`case.scad` is a parametric OpenSCAD case for the CrowPanel ESP32 2.13" E-Paper HMI
Display. **The dimensions are measured, not guessed.** They come from Elecrow's own CAD
assembly (`00-2-13_view_asm.stp`, in the `3D file` folder of
[Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250)),
opened in FreeCAD and queried part by part. The write-up is in
[`reference/BOARD_GEOMETRY.md`](reference/BOARD_GEOMETRY.md); the raw dump of all 519
solids is in `reference/step_parts_dump.json`.

Every position was then re-measured independently off `docs/images/hardware-overview.webp`
by pixel analysis, using the board outline as the scale reference. The two methods agree
to better than 0.4 mm on every control. Where they disagree, the CAD data wins.

## The board, in one table

| | |
|---|---|
| PCB outline | **31.2 x 63.2 mm**, 2.1 mm thick, 1 mm corner radius |
| e-paper module | 29.2 x 59.2 mm, standing 0.9 mm proud of the display face |
| Active area | 23.71 x 48.55 mm (122 x 250 px at 0.1942 mm pitch) |
| Mounting holes | **none** — see below |

## Which wall carries what

The board's long axis is horizontal in use (the firmware runs `setRotation(1)`).

- **Left wall** — MENU, rotary wheel, EXIT, at measured centres of −11.15, 0 and
  +11.15 mm across the board's width. All three are edge-mounted, actuate along the
  board's −Z, and physically stick out past the PCB outline: the rotary by 2.75 mm, MENU
  and EXIT by 0.8 mm each. They get wall openings, not face holes.
- **Bottom wall** — USB-C only, centred lengthwise (board Z = +0.60), flush with the
  board edge. The 5 mm-thick long wall is locally recessed back to 1.6 mm around the
  port so a plug can actually seat: the socket mouth is flush with the PCB edge, so every
  millimetre of wall eats into plug engagement.
  UART0 and BAT deliberately get **no** exterior cutout — UART0 is a debug/flashing
  console, and BAT is a JST pigtail meant to be plugged in before the case is closed.
  Revisit if you want battery hot-swap without opening the case.
- **Back floor** — **both** a BOOT pinhole and a RESET pinhole, Ø1.8 mm, paperclip-sized.
  These two switches are not on any edge: they sit 1.4 mm inboard of the board's top edge
  and protrude 6 mm out of the *component* face, so they are pressed from behind, through
  the floor, not from a side.
- **Front frame** — the screen aperture, and nothing else.
- **Top wall, right wall** — nothing but the case screws.

## Two printed pieces

- `back_shell()` — the tray. Owns the four walls, the left-wall openings, the USB-C
  opening and its recess, the two floor pinholes, the PCB support ledge, and the four
  screw bosses. Behind the board is a **flat, uniform-depth 8 mm cavity** — no battery
  pocket, no ribs, no raised island. A cell just lies in whatever space the components
  leave.
- `front_frame()` — a flat plate with the screen aperture and a clamp rib that holds the
  PCB down against the shell's ledge.

**There are no button extenders, plungers or actuator pieces, on purpose.** The reach
numbers work out without them:

- the rotary wheel's rim ends up **0.75 mm proud of the outer wall face**, so a thumb
  reaches it directly;
- MENU and EXIT end up **1.2 mm recessed** behind the outer face, and their openings are
  6.4 x 4.6 mm against a nub that is about 2.9 x 0.8 mm — a fingertip enters the opening
  and deforms well past 1.2 mm to reach the switch.

If you want a flusher feel on MENU/EXIT, deepen `OP_CHAMFER` or drop `WALL` to 1.2. Do
not add a plunger.

## The board has no mounting holes

`reference/BOARD_GEOMETRY.md` originally reported four Ø2.0 mm mounting holes at
(±14.60, ±30.60). Those cylindrical faces are real, but they are the PCB outline's
**corner fillets**, not holes — a Ø2.0 hole centred 1.0 mm from the edge would be tangent
to that edge, three of the four positions sit under the MENU/EXIT switch bodies, and the
product photo shows plainly rounded corners and no holes. (The "two Ø1.0 mm holes at
(±7.00, 30.30)" are likewise the rounded ends of the FPC pass-through slot.) That file
now carries the correction.

So the board is **clamped, not screwed**: it rests on a 0.8 mm perimeter ledge in the
shell and is held by a matching rib on the frame directly opposite, both interrupted
wherever a measured component would foul them. The two case halves screw to each other
with four **M2 x 8 self-tapping screws** driven from the front into the thickened long
walls, entirely outside the PCB footprint. That thickening is why the case is 42.0 mm
wide for a 31.2 mm board — with buttons on one short end and USB-C flush on one long
edge, the long walls are the only place a screw can live.

Overall closed size: **67.2 x 42.0 x 15.2 mm**.

## Verifying a change

`case.scad` carries its own verification geometry. Nothing here needs the physical board.

```sh
SCAD="C:/Program Files/OpenSCAD/openscad.exe"

# 1. Geometric check. `probes()` is the volume each control must have to itself:
#    the MENU/EXIT nubs swept outwards, the rotary's FULL swept disc (so this
#    tests rotational clearance, not just a hole), a USB-C plug, and two
#    paperclips. The intersection with the case must come out EMPTY.
"$SCAD" -o /tmp/interference.stl -D 'PART="interference"' case.scad
#    -> "Current top level object is empty" / a 47-byte STL means it passed.

# 2. Visual check. Orthographic elevations, not isometric — isometric is
#    ambiguous about which opening is where.
"$SCAD" -o left.png   --projection=o --camera=0,0,7,90,0,270,58  -D 'PART="shell"' case.scad
"$SCAD" -o floor.png  --projection=o --camera=0,0,7,180,0,0,110  -D 'PART="shell"' case.scad
"$SCAD" -o top.png    --projection=o --camera=0,0,7,0,0,0,110    -D 'PART="shell"' case.scad

# 3. Eyeball the parts in context, with the measured board and the probes drawn in.
"$SCAD" -D 'PART="assembly"' case.scad
```

The interference check is only worth trusting if it is sensitive, so confirm it: nudging
`probes()` by 1.2 mm in z or 2.0 mm in y turns the empty result into a 28 KB / 67 KB STL.

## Print settings

0.2 mm layer height, 3 perimeters, 15–20% infill. No supports needed in the orientation
the file lays the parts out in (`PART="plate"`): the shell prints floor-down and the
frame prints face-down. PETG or PLA are both fine; PETG if it will sit somewhere warm.

Watch two thin features when slicing: the ~1 mm of wall left between the flared mouths of
the MENU and rotary openings, and the ~1 mm of wall above the USB-C opening. Both are
above the 0.4 mm nozzle's comfortable minimum but do not shrink them further without
re-running the checks above.

## What the measured data still does not answer

- Component **heights** above the component face, beyond the bounding boxes. Enough for
  clearance, not enough for a snug fit — so how much of the 8 mm cavity a battery can
  actually use is unknown until you have the board in hand.
- The **active area's offset within the glass**. Only its size is derivable. The frame
  aperture is therefore the active area plus 1 mm all round.
- Whether the FPC ribbon bulges past the board's right-hand edge as it folds through its
  slot. The STEP says no (the module's bbox stops 1.15 mm short); the photo is
  ambiguous. `CLEAR` is 0.4 mm, which should absorb it.

## Prior art worth looking at

- [Case for CrowPanel 2.13" Epaper (Printables)](https://www.printables.com/model/1566902-case-for-crowpanel-213-epaper/related) —
  built for this exact board, a plain two-part protective shell with no battery bay.
- [Enclosure for CrowPanel ESP32 4.2" (Printables)](https://www.printables.com/model/1482686-enclosure-for-a-crowpanel-esp32-42inch-e-paper-dis/related) —
  wrong board, but explicitly built to cover the switches and hide a battery, so it is
  useful as a layout reference only.
