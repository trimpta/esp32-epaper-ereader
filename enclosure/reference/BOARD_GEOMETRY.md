# CrowPanel 2.13" board geometry — measured, not guessed

Every number here was extracted from Elecrow's own CAD assembly
(`3D file/00-2-13_view_asm.rar` in
[Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250](https://github.com/Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250)),
opened in FreeCAD and queried per-part. This supersedes every placeholder dimension the
enclosure previously used.

## Reproducing this

```python
# freecadcmd extract.py  —  the Import module, NOT Part.insert (loses part labels)
# and NOT ImportGui (won't load headless).
import FreeCAD, Import
doc = FreeCAD.newDocument("probe")
Import.insert(r"...\00-2-13_view_asm.stp", "probe")
doc.recompute()
for obj in doc.Objects:
    if hasattr(obj, "Shape") and not obj.Shape.isNull():
        print(obj.Label, obj.Shape.BoundBox)
```

The full dump of all 519 objects is in `step_parts_dump.json` next to this file.

## Coordinate system

The assembly is in the board's **native portrait** frame, in millimetres:

| Axis | Span | Meaning |
|---|---|---|
| **X** | −15.60 … +15.60 (31.20 mm) | Board width — matches the published 31.2 mm display "H" |
| **Y** | −2.10 … 0.00 (2.10 mm) | Board thickness. **0 is the display face; −Y is the component side** |
| **Z** | −31.60 … +31.60 (63.20 mm) | Board length — matches the published 63.19 mm display "L" |

### Mapping to what you see

Viewed **from behind** (looking at the component side, the way Elecrow's product photo shows
the PCB), confirmed against both the photo and the owner's direct description:

- **Z<sub>min</sub> (−31.6) is the LEFT edge**, Z<sub>max</sub> (+31.6) is the RIGHT edge
- **X<sub>min</sub> (−15.6) is the TOP edge**, X<sub>max</sub> (+15.6) is the BOTTOM edge

This holds together on every part below: the buttons land on the left, USB-C lands centred
on the bottom, BOOT/RESET land top-right. Note the reader is used in **landscape** (the
firmware does `setRotation(1)`), so the board's long Z axis is horizontal in use.

## Parts

Bounding boxes are global, in the frame above.

| Part | STEP label | X | Y | Z |
|---|---|---|---|---|
| PCB | `2_13_PCB` | −15.60 … 15.60 | −2.10 … 0.00 | −31.60 … 31.60 |
| Display module | `2_13-LCD` | −14.60 … 14.60 | −2.084 … 0.90 | −29.60 … 30.448 |
| **MENU** | `KEY-579_ASM` | −15.00 … −7.30 | −5.10 … −1.60 | −32.40 … −28.90 |
| **Rotary encoder** | `XB-TM-2024A` | −6.582 … 6.582 | −3.80 … −1.00 | −34.349 … −25.35 |
| **EXIT** | `KEY-579_ASM001` | 7.30 … 15.00 | −5.10 … −1.60 | −32.40 … −28.90 |
| **BOOT** | `TACT_SWITCH_ASM_2_1_ASM` | −14.175 … −9.625 | −8.10 … −1.584 | 11.45 … 18.95 |
| **RESET** | `TACT_SWITCH_ASM_2_1_ASM001` | −14.175 … −9.625 | −8.10 … −1.584 | 21.92 … 29.42 |
| **USB-C** | `TYPE_C_PORT_SMD_TYPE__ASM_2_1_ASM` | 7.70 … 15.60 | −4.81 … −0.61 | −3.87 … 5.07 |

## What the numbers mean for cutouts

**MENU, Rotary and EXIT are edge-mounted on the left (Z<sub>min</sub>) edge and actuate along
−Z.** All three extend *past* the PCB outline at that edge — the rotary by 2.75 mm
(Z −34.349 vs. the board's −31.60), MENU and EXIT by 0.8 mm each. That protrusion is the
part that has to come through the case wall, and it's the reason these want a wall opening
rather than a face hole.

Across the left edge, top to bottom: **MENU** (centre X ≈ −11.15), **Rotary** (centre X = 0),
**EXIT** (centre X ≈ +11.15).

**BOOT and RESET are not on any edge.** They sit 1.4 mm inboard of the top edge and protrude
6 mm out the **component face** (−Y), so they're pressed from behind the board, not from a
side. Centres: BOOT at Z ≈ 15.2, RESET at Z ≈ 25.67, both at X ≈ −11.9. Pinholes through
whatever panel covers the component side, not wall cutouts.

**USB-C sits flush with the bottom (X<sub>max</sub>) edge**, centred lengthwise (Z ≈ 0.6),
spanning Z −3.87 … 5.07 (≈ 8.9 mm wide) and 4.2 mm deep in Y. Unlike the buttons it doesn't
overhang the board outline, so the wall opening needs to clear the *plug*, not the socket.

**The rotary needs rotational clearance, not just a hole** — and it is a flat in-plane
**thumbwheel**, not a shaft with a knob. Its X span *is* its full diameter (a circle
reaches its X extremes at its centre's Z), so:

| | |
|---|---|
| diameter | 13.164 mm (radius 6.582) |
| centre | X = 0, Z = **−27.767** (= −34.349 + 6.582) |
| thickness | 2.80 mm (Y −3.80 … −1.00) |

The STEP solid is only about a 223° arc of that circle, which is why its Z span (9.0 mm)
is smaller than its diameter — the wheel is a C shape open towards the board interior.
The product photo shows exactly that: a toothed wheel of about that size, its rim poking
out of the left edge. A wall opening must clear the swept circle, which at 0.4 mm outside
the board edge is 2 × 5.04 = 10.08 mm across.

## Independent cross-check against the product photo

Every position above was re-measured by pixel analysis of
`docs/images/hardware-overview.webp`, using the detected board outline as the scale
reference (13.85 px/mm). Independent of the STEP file:

| Feature | Photo | STEP | Delta |
|---|---|---|---|
| MENU centre, board X | −11.16 | −11.150 | 0.01 |
| EXIT centre, board X | +11.16 | +11.150 | 0.01 |
| Rotary width, board X | 13.29 | 13.164 | 0.13 |
| Rotary protrusion past the edge | 2.1–2.7 | 2.749 | – |
| MENU/EXIT protrusion past the edge | 0.83 | 0.80 | 0.03 |
| BOOT centre, board Z | +15.23 | +15.200 | 0.03 |
| RESET centre, board Z | +25.60 | +25.670 | 0.07 |
| USB-C centre, board Z | +0.40 | +0.600 | 0.20 |
| BOOT/RESET cap centre, board X | −12.24 / −12.28 | −11.900 | 0.35 |

The last row is the only one over 0.2 mm, and it compares the visible cap against the
whole switch assembly's bbox centre, partly occluded by the photo's annotation dot. The
enclosure's pinholes are oversized (Ø1.8) to absorb it.

## Mounting holes — there are none. CORRECTED.

> **Correction, made while building the enclosure.** An earlier revision of this file
> read the PCB solid's cylindrical faces as mounting holes. They are not holes. The
> board has **no mounting holes at all**. What follows is the corrected reading.

Walking the PCB solid's cylindrical faces (they're cut features in the board, not
separate parts, so they don't appear in the parts dump) turns up six, and only six:

| Count | Radius | Positions (X, Z) | What they actually are |
|---|---|---|---|
| 4 | 1.00 mm | centred (±14.60, ±30.60) | the **outline's corner fillets** |
| 2 | 0.50 mm | centred (±7.00, 30.30) | the rounded **ends of the FPC slot** |

Why they are not holes:

- A Ø2.00 hole whose centre sits exactly 1.00 mm from the edge is *tangent* to that edge —
  it would break out of the board. Not manufacturable. R1.00 centred 1.00 mm in from both
  edges, at all four corners, is instead the exact signature of a 1 mm corner round, and
  the product photo shows corners rounded at about that radius.
- Three of the four positions sit underneath a switch body. MENU spans X −15.000…−7.300
  at Z −32.400…−28.900, which contains (−14.60, −30.60); EXIT likewise contains
  (+14.60, −30.60). Nothing could be screwed there.
- The two R0.50 faces at (±7.00, 30.30) are 14.0 mm apart on one line — the two ends of a
  14.0 × 1.0 mm slot at Z = 30.30, 1.30 mm in from the right edge. That slot is plainly
  visible in the product photo at exactly that spot, with the display's FPC passing
  through it.
- Elecrow's wiki lists no mounting holes, and none are visible anywhere on the
  component-side photo.

Consequence for an enclosure: the board cannot be screwed down. It has to be clamped —
supported on a ledge around its perimeter and held by an opposing rib, both interrupted
where the components above reach the board edge.

## Still not measured

- Component *heights* above the component face beyond the bounding boxes above — enough for
  clearance, not enough to model a snug fit. In particular, how much of a back cavity a
  battery can really use is unknown until the board is in hand.
- The e-paper **active area's offset within the glass**. Its size is derivable
  (122 × 250 px at the panel's 0.1942 mm pitch = 23.71 × 48.55 mm) and the glass outline
  is (29.2 × 59.2, board Z −29.60 … +29.60 — the module bbox's extra 0.85 mm to +30.448
  is the FPC tail). Where the active rectangle sits inside that is not in the dump.
- Where the MENU/EXIT **actuator nub** sits within its switch's 7.7 mm-wide body. The
  photo puts the nub at about 2.9 mm tall and roughly centred, but the STEP solid is one
  lump.
- Whether the display's FPC bulges past the board's right-hand edge as it folds through
  the slot. The STEP says no; the photo is ambiguous.
- Anything about the acrylic shell Elecrow ships, which is in the same assembly but wasn't
  extracted here.
