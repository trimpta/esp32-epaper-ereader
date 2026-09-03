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

**The rotary needs rotational clearance, not just a hole.** Its body spans 13.16 mm across X
and 9 mm along Z.

## Mounting holes

Found by walking the PCB solid's cylindrical faces (they're cut features in the board, not
separate parts, so they don't appear in the parts dump):

| Count | Diameter | Positions (X, Z) | Inset from edge |
|---|---|---|---|
| 4 | **2.00 mm** | (±14.60, ±30.60) | 1.00 mm from both nearest edges |
| 2 | **1.00 mm** | (±7.00, 30.30) | 1.30 mm from the right edge |

The four 2 mm holes are the mounting points. M2 hardware clears them directly; the previous
enclosure guessed 2.2 mm at a 3 mm inset, which would have missed every one of them.

## Still not measured

- Component *heights* above the component face beyond the bounding boxes above — enough for
  clearance, not enough to model a snug fit.
- Whether the 1 mm holes are alignment pins, test points, or unused.
- Anything about the acrylic shell Elecrow ships, which is in the same assembly but wasn't
  extracted here.
