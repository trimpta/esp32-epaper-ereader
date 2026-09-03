// ============================================================================
//  Parametric two-piece enclosure for the CrowPanel ESP32 2.13" E-Paper HMI
//  Display (Elecrow, 122x250, SPI).
// ============================================================================
//
//  WHERE THE NUMBERS COME FROM
//  ---------------------------
//  Every board dimension in this file is measured, not guessed. The source is
//  Elecrow's own CAD assembly (00-2-13_view_asm.stp, from
//  Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250), opened in
//  FreeCAD and queried part-by-part. The extraction, the per-part bounding
//  boxes and the reproduction recipe live in enclosure/reference/ —
//  BOARD_GEOMETRY.md for the write-up, step_parts_dump.json for the raw dump of
//  all 519 solids.
//
//  Every one of those positions was then independently re-measured off the
//  annotated product photo (docs/images/hardware-overview.webp) by pixel
//  analysis, using the board outline as the scale reference. The two methods
//  agree to better than 0.4 mm on every control (see the cross-check table
//  below). Where they disagree, the STEP data wins.
//
//  The only numbers here that are NOT from the STEP file are:
//    * the e-paper active-area size (23.71 x 48.55 mm), which comes from the
//      122x250 pixel count at the panel's 0.1942 mm pitch, and
//    * generic hardware sizes (M2 screws, USB-C plug envelope, paperclip).
//  Both are flagged where they are used.
//
//  BOARD COORDINATE SYSTEM (the STEP assembly's own frame)
//  ------------------------------------------------------
//    board X : -15.60 .. +15.60   board width   (31.20 mm)
//    board Y :  -2.10 ..   0.00   board thickness; Y=0 is the DISPLAY face,
//                                 -Y is the COMPONENT side
//    board Z : -31.60 .. +31.60   board length  (63.20 mm)
//  Viewed from behind: Z-min is the LEFT edge, X-min is the TOP edge.
//
//  CASE COORDINATE SYSTEM used below (a cyclic, right-handed permutation, so
//  nothing is mirrored):
//    case x = board Z    (long axis; left wall at x-min)
//    case y = board X    (short axis; USB-C wall at y-max)
//    case z = PCB_FACE_Z + board Y   (0 = outside of the back shell's floor)
//  So a feature at board (X, Y, Z) is at case (Z, X, PCB_FACE_Z + Y).
//
//  WHAT LIVES ON WHICH FACE
//  ------------------------
//    LEFT wall  (x-min): MENU, rotary wheel, EXIT. All three are edge-mounted
//                        and actuate along -Z, and all three physically stick
//                        out past the PCB outline, so they need wall openings.
//    BOTTOM wall (y-max): USB-C, flush with the board edge, centred lengthwise.
//    BACK floor (z-min): BOOT and RESET pinholes. These two are NOT on an edge —
//                        they sit 1.425 mm inboard of the top edge and protrude
//                        6.0 mm out of the component face, so they are pressed
//                        from behind, through the floor.
//    FRONT frame:        screen aperture only.
//    TOP wall, RIGHT wall: nothing.
//
//  NO BUTTON EXTENDERS. There are deliberately no plungers, actuator pieces or
//  printed caps in this file. Direct cutouts only. The reach numbers are worked
//  out in the "ACTUATOR REACH" note further down.
//
//  ============================================================================
//  DERIVATION CHECK — cutout centre vs. the measured part it comes from
//  ============================================================================
//  Every opening centre below is the arithmetic midpoint of the measured
//  bounding box, converted into case coordinates. Checked before modelling:
//
//   control  measured box (board frame)              -> cutout centre (case frame)
//   -------  --------------------------------------  ----------------------------
//   MENU     X -15.000..-7.300  Y -5.10..-1.60        y = -11.150   z = 8.350
//   EXIT     X   7.300..15.000  Y -5.10..-1.60        y = +11.150   z = 8.350
//   ROTARY   X  -6.582.. 6.582  Y -3.80..-1.00        y =   0.000   z = 9.300
//   USB-C    Z  -3.870.. 5.070  Y -4.81..-0.61        x =  +0.600   z = 8.990
//   BOOT     Z  11.450..18.950  X -14.175..-9.625     x = +15.200   y = -11.900
//   RESET    Z  21.920..29.420  X -14.175..-9.625     x = +25.670   y = -11.900
//
//   (z = PCB_FACE_Z + midpoint of the Y span, PCB_FACE_Z = 11.700.
//    e.g. MENU: 11.700 + (-5.10 + -1.60)/2 = 11.700 - 3.350 = 8.350.)
//
//  Independent cross-check, measured off docs/images/hardware-overview.webp by
//  pixel analysis (board outline = scale reference, 13.85 px/mm):
//
//   control  photo                     STEP        delta
//   -------  ------------------------  ----------  --------
//   MENU     board X -11.16            -11.150     0.01 mm
//   EXIT     board X +11.16            +11.150     0.01 mm
//   ROTARY   board X span 13.29 wide    13.164     0.13 mm
//   ROTARY   protrudes 2.1..2.7 past the edge (2.749 in STEP)
//   MENU/EXIT protrude 0.83 past the edge (0.80 in STEP)
//   BOOT     board Z +15.23            +15.200     0.03 mm
//   RESET    board Z +25.60            +25.670     0.07 mm
//   USB-C    board Z + 0.40             +0.600     0.20 mm
//   BOOT/RESET board X -12.24/-12.28   -11.900     0.35 mm (cap vs. body
//              centre; the pinholes are oversized to absorb this)
//
//  ============================================================================
//  CORRECTION TO reference/BOARD_GEOMETRY.md: THE BOARD HAS NO MOUNTING HOLES
//  ============================================================================
//  BOARD_GEOMETRY.md reported "4 x Ø2.00 mm mounting holes at (+/-14.60,
//  +/-30.60), 1.00 mm from both nearest edges". Those cylindrical faces are
//  real, but they are not holes — they are the PCB outline's CORNER FILLETS:
//
//    * A Ø2.0 hole whose centre sits exactly 1.0 mm from the edge would be
//      tangent to that edge, i.e. it would break out of the board. Not
//      manufacturable.
//    * R1.0 centred 1.0 mm in from both edges at all four corners is the exact
//      signature of a 1 mm corner round, and the product photo shows rounded
//      corners of about that radius.
//    * Three of the four positions sit underneath the MENU and EXIT switch
//      bodies (MENU spans board X -15.000..-7.300 at Z -32.400..-28.900, which
//      contains (-14.60, -30.60)) — impossible for a screw hole.
//    * The remaining "2 x Ø1.00 mm holes at (+/-7.00, 30.30)" are the rounded
//      ends of the FPC pass-through SLOT, ~14.0 x 1.0 mm at board Z = 30.30.
//      The slot is plainly visible in the product photo at that exact spot.
//    * Elecrow's own wiki lists no mounting holes, and none are visible
//      anywhere on the component-side photo.
//
//  So there are no PCB screw points, and this enclosure does not pretend there
//  are. The board is CLAMPED instead: it rests on a 0.8 mm perimeter ledge in
//  the back shell and is held down by a matching rib on the front frame,
//  directly opposite. Both are interrupted wherever a measured component would
//  foul them (see KEEPOUTS). The two case halves screw to each other through
//  the thickened long walls, entirely outside the PCB footprint.
//
//  ============================================================================

$fn = 48;

// ---------------------------------------------------------------------------
// 1. MEASURED BOARD DATA  (board frame, millimetres, from the STEP assembly)
// ---------------------------------------------------------------------------

PCB_W  = 31.2;    // board X span, -15.60 .. +15.60
PCB_L  = 63.2;    // board Z span, -31.60 .. +31.60
PCB_T  =  2.1;    // board Y span,  -2.10 ..   0.00
PCB_CORNER_R = 1.0;   // outline corner fillet (see the correction note above)

// e-paper module (2_13-LCD): sits on the display face and stands 0.90 mm proud.
GLASS_W  = 29.2;  // board X, -14.60 .. +14.60  -> 1.0 mm of PCB margin each side
GLASS_L  = 59.2;  // board Z, -29.60 .. +29.60  (the STEP bbox runs to +30.448;
                  //   that extra 0.85 mm is the FPC tail, not glass)
GLASS_H  =  0.9;  // board Y,   0.00 ..  +0.90
// Active area: NOT in the STEP file. 122 x 250 pixels at the panel's 0.1942 mm
// pitch => 23.71 x 48.55 mm. Its offset within the glass is also not measured,
// so the frame aperture below is deliberately oversized to absorb it.
ACTIVE_W = 23.71; // board X
ACTIVE_L = 48.55; // board Z

// Measured part bounding boxes, board frame: [Xmin, Xmax, Ymin, Ymax, Zmin, Zmax]
MENU_BOX   = [-15.000,  -7.300, -5.100, -1.600, -32.400, -28.900];
EXIT_BOX   = [  7.300,  15.000, -5.100, -1.600, -32.400, -28.900];
ROTARY_BOX = [ -6.582,   6.582, -3.800, -1.000, -34.349, -25.350];
BOOT_BOX   = [-14.175,  -9.625, -8.100, -1.584,  11.450,  18.950];
RESET_BOX  = [-14.175,  -9.625, -8.100, -1.584,  21.920,  29.420];
USBC_BOX   = [  7.700,  15.600, -4.810, -0.610,  -3.870,   5.070];

// FPC pass-through slot in the PCB (the "2 x Ø1.0 holes" of BOARD_GEOMETRY.md).
FPC_SLOT_Z = 30.30;  FPC_SLOT_W = 14.0;  FPC_SLOT_H = 1.0;

// The rotary control is a flat in-plane thumbwheel, not a shaft-and-knob. Its
// X span IS its full diameter (a circle reaches its X extremes at its centre's
// Z), so: radius = 13.164/2 = 6.582, centre at board Z = -34.349 + 6.582.
// The STEP solid is only a ~223 degree arc of it, which is why its Z span
// (9.0 mm) is less than its diameter. The photo confirms a toothed C-shaped
// wheel of that size, open towards the board interior.
ROTARY_R      = (ROTARY_BOX[1] - ROTARY_BOX[0]) / 2;      // 6.582
ROTARY_CTR_Z  =  ROTARY_BOX[4] + ROTARY_R;                // -27.767
ROTARY_THK    =  ROTARY_BOX[3] - ROTARY_BOX[2];           // 2.80

// ---------------------------------------------------------------------------
// 2. HELPERS — measured box -> case-frame centre
// ---------------------------------------------------------------------------
// case x = board Z, case y = board X, case z = PCB_FACE_Z + board Y

function bx(b) = (b[4] + b[5]) / 2;                 // case x centre
function by(b) = (b[0] + b[1]) / 2;                 // case y centre
function bz(b) = PCB_FACE_Z + (b[2] + b[3]) / 2;    // case z centre

// ---------------------------------------------------------------------------
// 3. SHELL PARAMETERS
// ---------------------------------------------------------------------------

CLEAR = 0.4;      // gap between the PCB outline and the cavity walls, all round

// Two different wall thicknesses, for a reason:
//  * WALL (left and right walls) is thin because the left wall has to let the
//    edge-mounted actuators through and the right wall has nothing to do.
//  * SIDE_WALL (top and bottom, the long walls) is thick because it is the ONLY
//    place a case screw can live — the PCB has no holes and both short ends are
//    occupied (buttons at x-min, USB-C flush at y-max). The USB-C region of the
//    bottom wall is locally recessed back to WALL so a plug can still seat.
WALL      = 1.6;
SIDE_WALL = 5.0;

FLOOR      = 1.6;   // back shell floor thickness
FRONT_T    = 2.6;   // front frame plate thickness
CORNER_R   = 3.0;   // outer profile corner radius

// Cavity depth behind the PCB. Driven by BOOT/RESET: they protrude 6.00 mm out
// of the component face (board Y -8.100 vs the PCB's -2.100), so anything less
// than 6 mm would crush them. 8.0 mm leaves them 2.0 mm clear of the floor and
// leaves a flat, uniform-depth volume for a pouch cell.
//
// BATTERY: there is deliberately NO battery pocket, rib, wall or raised island.
// The cavity floor is one flat plane at z = FLOOR across its whole area. The
// cell simply lies in whatever space the components leave; how much that is
// depends on the cell and on component heights this data does not give (see
// "still not measured" in BOARD_GEOMETRY.md). The two paperclip holes come up
// through this floor, so keep the cell clear of BOOT/RESET.
CAVITY_H = 8.0;

// Derived z datums (case frame; z = 0 is the outside of the floor)
PCB_BACK_Z = FLOOR + CAVITY_H;          //  9.6  — component-side face of the PCB
PCB_FACE_Z = PCB_BACK_Z + PCB_T;        // 11.7  — display-side face of the PCB
RIM_Z      = PCB_FACE_Z + GLASS_H;      // 12.6  — top of the back shell's walls,
                                        //         level with the top of the glass
CASE_H     = RIM_Z + FRONT_T;           // 15.2  — overall closed height

// Outer half-extents
CASE_HX = PCB_L / 2 + CLEAR + WALL;       // 33.6  -> 67.2 long
CASE_HY = PCB_W / 2 + CLEAR + SIDE_WALL;  // 21.0  -> 42.0 wide
// Cavity half-extents (wall inner faces)
CAV_HX  = PCB_L / 2 + CLEAR;              // 32.0
CAV_HY  = PCB_W / 2 + CLEAR;              // 16.0
// Cavity corner radius. Kept below 1.8 so that the cavity's corner arc stays
// clear of the PCB's own R1.0 outline fillets: at CAV_R = 1.6 the corner
// clearance is 0.32 mm, at CAV_R = 2.0 it would drop to 0.15 mm.
CAV_R   = 1.6;

// PCB clamp: a ledge in the shell and an opposing rib on the frame, both
// overlapping the board edge by LEDGE_OVERLAP. 0.8 mm keeps the front rib 0.2 mm
// clear of the glass, which starts 1.0 mm in from the long edges.
LEDGE_OVERLAP = 0.8;
LEDGE_W       = CLEAR + LEDGE_OVERLAP;   // 1.2 measured from the wall inner face
LEDGE_T       = 1.2;                     // ledge thickness (z 8.4 .. 9.6)

// Case screws: M2 self-tapping, driven from the front into the thick long walls.
SCREW_X       = 22.0;                    // case x of the four screws
SCREW_Y       = CAV_HY + SIDE_WALL / 2;  // 18.5 — centred in the thick wall
SCREW_PILOT_D = 1.7;                     // tapping hole in the shell
SCREW_PILOT_H = 8.0;
SCREW_FREE_D  = 2.3;                     // clearance hole in the frame
SCREW_HEAD_D  = 4.2;                     // counterbore for an M2 pan head
SCREW_HEAD_H  = 1.4;

// ---------------------------------------------------------------------------
// 4. OPENINGS
// ---------------------------------------------------------------------------
//
// ACTUATOR REACH — checked against the measured protrusions, because this is
// what decides whether a plain cutout is enough (it is) or a plunger would be
// needed (it is not, and none is provided):
//
//   left wall: inner face at x = -32.0, outer face at x = -33.6 (1.6 mm thick)
//
//   * ROTARY wheel tip is at board Z = -34.349, i.e. 0.749 mm PROUD of the
//     outer wall face. The thumb reaches it directly through the opening; the
//     0.8 mm outer chamfer on the opening exposes a little more of the rim.
//   * MENU / EXIT nubs reach board Z = -32.400, i.e. 0.400 mm into the wall and
//     therefore 1.200 mm RECESSED behind the outer face. That gap is not
//     bridged by a printed part, on purpose: the openings are 7.0 x 4.6 mm,
//     which is far larger than the ~2.9 x 0.8 mm nub, so a fingertip enters the
//     opening and deforms well past 1.2 mm to press the switch. If you want a
//     flusher feel, deepen the chamfer or drop WALL to 1.2 — do not add a
//     plunger.
//
// MENU/EXIT opening size. Sized against the nub (about 2.9 mm tall, measured
// off the photo, x 0.8 mm proud) with plenty of room for a fingertip, but
// capped so that the flared mouths of MENU and the rotary do not eat the wall
// between them: at 6.4 mm wide with a 0.6 mm flare, 1.0 mm of wall survives
// between the two mouths (it was 0.3 mm at 7.0 mm / 0.8 mm — too thin to print).
BUTTON_OP_Y = 6.4;    // MENU/EXIT opening, along case y (board X)
BUTTON_OP_Z = 4.6;    // MENU/EXIT opening, along case z
OP_CHAMFER  = 0.6;    // 45-degree flare on the outer edge of the left-wall openings

// Rotary opening. Sized from the wheel's swept circle, not from a token hole:
// at the wall's INNER face the wheel's half-width is
//   sqrt(6.582^2 - (32.0 - 27.767)^2) = 5.040 mm,
// so an 11.5 mm opening (+/-5.75) clears the sweep by 0.71 mm a side. Height
// 3.8 mm against the wheel's 2.8 mm thickness, i.e. 0.5 mm a side — kept down
// so the flared mouth still leaves 0.8 mm of rim above it.
ROTARY_OP_Y = 11.5;
ROTARY_OP_Z =  3.8;

// USB-C. Must clear a PLUG, not the socket. The socket is 8.94 x 4.20 mm; a
// USB-C plug's shell is 8.44 x 2.66 mm max, and its overmold nose is typically
// 10.5-12.5 x 5.5-7 mm. Two things are done for it:
//   1. the opening is 11.0 x 4.8 mm — bigger than the socket in both axes, and
//      comfortably bigger than the plug shell;
//   2. the bottom wall is locally recessed from 5.0 mm back to WALL (1.6 mm)
//      over a 16.0 x 9.1 mm patch, so the overmold sits INSIDE the wall instead
//      of holding the plug 5 mm short of the socket. The socket mouth is flush
//      with the PCB edge, so every millimetre of wall eats into plug engagement;
//      1.6 + 0.4 clearance = 2.0 mm of tunnel still leaves a normal plug fully
//      seated.
// If a fat cable will not seat, raise USBC_OP_Y / USBC_OP_Z or deepen the
// recess — those are the only knobs needed.
USBC_OP_Y   = 11.0;   // along case x (board Z)
USBC_OP_Z   =  4.8;   // along case z
USBC_REC_W  = 16.0;   // recess, along case x
USBC_REC_Z0 =  3.5;   // recess, lower z bound (upper bound is the rim)

// BOOT and RESET pinholes. Both are provided — the owner asked for both. The
// switches protrude 6.0 mm out of the component face and end 2.0 mm above the
// cavity floor, so a straightened paperclip (~1.0 mm) passes through the floor
// and presses them. Ø1.8 also absorbs the 0.35 mm difference between the switch
// body centre (STEP) and the visible cap centre (photo).
PIN_D       = 1.8;
PIN_CHAMFER = 0.6;

// Screen aperture. The active area's position within the glass is not measured,
// so the aperture is the active area plus a 1.0 mm margin all round; it still
// overlaps the glass by 1.7 mm on the long edges and 4.3 mm on the short ones,
// which is what holds the module down.
APERTURE_MARGIN = 1.0;

// ---------------------------------------------------------------------------
// 5. COMPONENT KEEP-OUTS (case frame) — where the clamp ledge/rib must not go
// ---------------------------------------------------------------------------
// [x0, x1, y0, y1], each derived from the measured box plus ~0.6 mm, widened
// where the wall opening is wider than the part.
KEEPOUTS = [
  // MENU:   box Z -32.400..-28.900, X -15.000..-7.300
  [-35.0, -28.3, -16.2,  -6.7],
  // EXIT:   box Z -32.400..-28.900, X   7.300..15.000
  [-35.0, -28.3,   6.7,  16.2],
  // ROTARY: swept circle r=6.582 about x=-27.767, plus the 11.5 mm opening
  [-35.0, -24.7,  -7.2,   7.2],
  // USB-C:  box Z -3.870..5.070, X 7.700..15.600, widened to the 11.0 mm opening
  [ -6.5,   7.7,   7.1,  16.2],
  // BOOT:   box Z 11.450..18.950, X -14.175..-9.625
  [ 10.8,  19.6, -14.8,  -9.0],
  // RESET:  box Z 21.920..29.420, X -14.175..-9.625
  [ 21.3,  30.0, -14.8,  -9.0],
];

module keepout_solids(z0, z1) {
  for (k = KEEPOUTS)
    translate([k[0], k[2], z0])
      cube([k[1] - k[0], k[3] - k[2], z1 - z0]);
}

// ---------------------------------------------------------------------------
// 6. PRIMITIVES
// ---------------------------------------------------------------------------

// Rounded rectangular prism, centred on x/y, rising from z0 to z1.
module rrect(hx, hy, r, z0, z1) {
  translate([0, 0, z0])
    hull()
      for (sx = [-1, 1], sy = [-1, 1])
        translate([sx * (hx - r), sy * (hy - r), 0])
          cylinder(r = r, h = z1 - z0);
}

// ---------------------------------------------------------------------------
// 7. BACK SHELL
// ---------------------------------------------------------------------------
// Owns: the four walls, the flat cavity, the left-wall openings, the USB-C
// opening and its recess, the two floor pinholes, the PCB ledge and the four
// screw bosses.

module back_shell() {
  difference() {
    union() {
      rrect(CASE_HX, CASE_HY, CORNER_R, 0, RIM_Z);
    }

    // --- cavity: one flat floor at z = FLOOR, uniform depth, no battery pocket
    rrect(CAV_HX, CAV_HY, CAV_R, FLOOR, RIM_Z + 1);

    // --- LEFT WALL: MENU / ROTARY / EXIT ------------------------------------
    // MENU  y = by(MENU_BOX)   = -11.150 , z = bz(MENU_BOX)   = 8.350
    // ROT   y = by(ROTARY_BOX) =   0.000 , z = bz(ROTARY_BOX) = 9.300
    // EXIT  y = by(EXIT_BOX)   = +11.150 , z = bz(EXIT_BOX)   = 8.350
    left_opening(by(MENU_BOX),   bz(MENU_BOX),   BUTTON_OP_Y, BUTTON_OP_Z);
    left_opening(by(ROTARY_BOX), bz(ROTARY_BOX), ROTARY_OP_Y, ROTARY_OP_Z);
    left_opening(by(EXIT_BOX),   bz(EXIT_BOX),   BUTTON_OP_Y, BUTTON_OP_Z);

    // --- BOTTOM WALL: USB-C -------------------------------------------------
    // x = bx(USBC_BOX) = +0.600 , z = bz(USBC_BOX) = 8.990
    // recess first (thins the 5.0 mm wall back to WALL so a plug can seat)
    translate([bx(USBC_BOX) - USBC_REC_W / 2, CAV_HY + WALL, USBC_REC_Z0])
      cube([USBC_REC_W, SIDE_WALL, RIM_Z - USBC_REC_Z0 + 1]);
    // then the opening itself
    bottom_opening(bx(USBC_BOX), bz(USBC_BOX), USBC_OP_Y, USBC_OP_Z);

    // --- FLOOR: BOOT and RESET pinholes -------------------------------------
    // BOOT  (x, y) = (+15.200, -11.900)
    // RESET (x, y) = (+25.670, -11.900)
    pinhole(bx(BOOT_BOX),  by(BOOT_BOX));
    pinhole(bx(RESET_BOX), by(RESET_BOX));

    // --- case screw pilot holes --------------------------------------------
    for (sx = [-1, 1], sy = [-1, 1])
      translate([sx * SCREW_X, sy * SCREW_Y, RIM_Z - SCREW_PILOT_H])
        cylinder(d = SCREW_PILOT_D, h = SCREW_PILOT_H + 1);
  }

  // --- PCB ledge (added after the difference so the cavity doesn't eat it) ---
  difference() {
    rrect(CAV_HX, CAV_HY, CAV_R, PCB_BACK_Z - LEDGE_T, PCB_BACK_Z);
    rrect(CAV_HX - LEDGE_W, CAV_HY - LEDGE_W, max(CAV_R - LEDGE_W, 0.5),
          PCB_BACK_Z - LEDGE_T - 1, PCB_BACK_Z + 1);
    keepout_solids(PCB_BACK_Z - LEDGE_T - 1, PCB_BACK_Z + 1);
  }
}

// Through-opening in the left (x-min) wall, with a 45-degree flare on the outer
// face so a fingertip can enter and so more of the rotary rim is exposed.
module left_opening(cy, cz, w, h) {
  // straight section, from inside the cavity out to the start of the flare
  translate([-CASE_HX - 1, cy - w / 2, cz - h / 2])
    cube([CASE_HX - CAV_HX + 2 + 1, w, h]);
  // flare
  hull() {
    translate([-CASE_HX + OP_CHAMFER, cy, cz]) cube([0.01, w, h], center = true);
    translate([-CASE_HX - 0.5, cy, cz])
      cube([0.01, w + 2 * OP_CHAMFER, h + 2 * OP_CHAMFER], center = true);
  }
}

// Through-opening in the bottom (y-max) wall, flared outwards.
module bottom_opening(cx, cz, w, h) {
  hull() {
    translate([cx, CAV_HY - 1, cz]) cube([w, 0.01, h], center = true);
    translate([cx, CASE_HY + 4, cz]) cube([w, 0.01, h], center = true);
  }
}

// Paperclip hole through the floor, chamfered on the outside for guidance.
module pinhole(cx, cy) {
  translate([cx, cy, -1]) cylinder(d = PIN_D, h = FLOOR + 2);
  translate([cx, cy, -0.01])
    cylinder(d1 = PIN_D + 2 * PIN_CHAMFER, d2 = PIN_D, h = PIN_CHAMFER);
}

// ---------------------------------------------------------------------------
// 8. FRONT FRAME
// ---------------------------------------------------------------------------
// Owns: the screen aperture, the clamp rib that holds the PCB against the
// shell's ledge, and the four screw counterbores. No button features — every
// control on this board is reached from an edge or from the back.

module front_frame() {
  difference() {
    union() {
      rrect(CASE_HX, CASE_HY, CORNER_R, RIM_Z, CASE_H);

      // clamp rib: mirror image of the shell's ledge, bearing on the PCB's
      // display-side margin, interrupted by the same keep-outs so the two
      // always oppose each other.
      difference() {
        rrect(CAV_HX, CAV_HY, CAV_R, PCB_FACE_Z, RIM_Z);
        rrect(CAV_HX - LEDGE_W, CAV_HY - LEDGE_W, max(CAV_R - LEDGE_W, 0.5),
              PCB_FACE_Z - 1, RIM_Z + 1);
        keepout_solids(PCB_FACE_Z - 1, RIM_Z + 1);
      }
    }

    // relief so the frame never presses on the glass itself. Kept 0.1 mm
    // narrower than the gap to the clamp rib (rib inner edge at +/-14.8, glass
    // edge at +/-14.6) so it does not undercut the rib.
    translate([-GLASS_L / 2 - 0.1, -GLASS_W / 2 - 0.1, RIM_Z - 0.01])
      cube([GLASS_L + 0.2, GLASS_W + 0.2, 0.3]);

    // screen aperture (case x = board Z = the long axis)
    translate([-(ACTIVE_L / 2 + APERTURE_MARGIN),
               -(ACTIVE_W / 2 + APERTURE_MARGIN), RIM_Z - 1])
      cube([ACTIVE_L + 2 * APERTURE_MARGIN,
            ACTIVE_W + 2 * APERTURE_MARGIN,
            FRONT_T + 2]);

    // screw clearance holes + counterbores
    for (sx = [-1, 1], sy = [-1, 1]) {
      translate([sx * SCREW_X, sy * SCREW_Y, RIM_Z - 1])
        cylinder(d = SCREW_FREE_D, h = FRONT_T + 2);
      translate([sx * SCREW_X, sy * SCREW_Y, CASE_H - SCREW_HEAD_H])
        cylinder(d = SCREW_HEAD_D, h = SCREW_HEAD_H + 1);
    }
  }
}

// ---------------------------------------------------------------------------
// 9. VERIFICATION GEOMETRY
// ---------------------------------------------------------------------------
// Not printed. These reproduce the measured parts in place so alignment can be
// checked geometrically rather than by eye.

module board_mockup() {
  // PCB
  color("darkgreen")
    translate([-PCB_L / 2, -PCB_W / 2, PCB_BACK_Z]) cube([PCB_L, PCB_W, PCB_T]);
  // glass
  color("gainsboro")
    translate([-GLASS_L / 2, -GLASS_W / 2, PCB_FACE_Z]) cube([GLASS_L, GLASS_W, GLASS_H]);
  // measured component boxes, drawn exactly as extracted
  for (b = [MENU_BOX, EXIT_BOX, BOOT_BOX, RESET_BOX, USBC_BOX])
    color("dimgray")
      translate([b[4], b[0], PCB_FACE_Z + b[2]])
        cube([b[5] - b[4], b[1] - b[0], b[3] - b[2]]);
  // rotary, as the wheel it actually is
  color("black")
    translate([ROTARY_CTR_Z, 0, PCB_FACE_Z + ROTARY_BOX[2]])
      cylinder(r = ROTARY_R, h = ROTARY_THK);
}

// Probe solids: the volume each control must have to itself. If the case
// intersects any of these, an opening is mispositioned or undersized.
module probes() {
  // MENU / EXIT actuator nubs, swept outwards past the case
  for (b = [MENU_BOX, EXIT_BOX])
    translate([b[4], by(b), bz(b)])
      rotate([0, -90, 0])
        cylinder(d = 2.9, h = 12);          // 2.9 mm = nub height measured in the photo
  // ROTARY: the full swept disc of the wheel, so this tests rotation clearance
  translate([ROTARY_CTR_Z, 0, PCB_FACE_Z + ROTARY_BOX[2]])
    cylinder(r = ROTARY_R, h = ROTARY_THK);
  // USB-C plug, in two parts because they are checked against different things:
  //   * the shell (8.44 x 2.66 max) has to pass THROUGH the wall opening and
  //     reach the socket mouth at board X = +15.60;
  //   * the overmold (12.4 x 6.5, a typical slim cable) only has to fit inside
  //     the wall recess without fouling it — it butts against the 1.6 mm wall,
  //     it does not pass through. So it starts at the recess face, y = 17.6.
  translate([bx(USBC_BOX), USBC_BOX[1] - 6.0, bz(USBC_BOX)])
    cube([8.44, 14.0, 2.66], center = true);
  translate([bx(USBC_BOX), CAV_HY + WALL + 6.0, bz(USBC_BOX)])
    cube([12.4, 12.0, 6.5], center = true);
  // BOOT / RESET paperclips, entering from outside the floor
  for (b = [BOOT_BOX, RESET_BOX])
    translate([bx(b), by(b), -6])
      cylinder(d = 1.0, h = 6 + PCB_FACE_Z + b[2] + 0.5);
}

// ---------------------------------------------------------------------------
// 10. RENDER SELECTOR
// ---------------------------------------------------------------------------
//   PART = "plate"        both pieces laid out for slicing   (default)
//   PART = "shell"        back shell only
//   PART = "frame"        front frame only
//   PART = "assembly"     closed case + board mockup + probes (visual check)
//   PART = "interference" case AND probes  -> must render EMPTY

PART = "plate";

if (PART == "plate") {
  // Both parts sitting on z = 0 in their print orientation: the shell floor-down,
  // the frame flipped face-down (a true 180-degree rotation about x, not a mirror).
  back_shell();
  translate([0, CASE_HY * 2 + 6, CASE_H]) rotate([180, 0, 0]) front_frame();
} else if (PART == "shell") {
  back_shell();
} else if (PART == "frame") {
  front_frame();
} else if (PART == "assembly") {
  %back_shell();
  %front_frame();
  board_mockup();
  color("red") probes();
} else if (PART == "interference") {
  intersection() {
    union() { back_shell(); front_frame(); }
    probes();
  }
}
