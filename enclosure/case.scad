// Parametric two-piece case for the CrowPanel ESP32 2.13" E-Paper board.
//
// Layout is derived from docs/images/hardware-overview.webp (annotated board photo).
// Board orientation used throughout this file: PCB_WIDTH = X (short axis), PCB_LENGTH =
// Y (long axis), Y=0 is the board's TOP short edge, Y=PCB_LENGTH is the BOTTOM short
// edge, X=0 is the LEFT long edge. Per the photo:
//   - LEFT edge (top to bottom): MENU button, Rotary Switch (encoder + knob), EXIT
//     button. All three are side-actuated (pressed/turned from the edge, not the face).
//   - TOP edge: GPIO_D (JST), BOOT button, RESET button. BOOT/RESET are ordinary
//     top-mounted tactiles (pressed straight down, from the display-facing side).
//   - BOTTOM edge: UART0 (JST), USB Type-C, BAT (JST).
//   - RIGHT edge: nothing.
//
// Two pieces:
//   front_frame() — a bezel over the display face. Owns the screen cutout and the
//     RESET pinhole, because both are accessed perpendicular to the board face.
//   back_shell()  — the piece with real wall height. Owns the LEFT-wall cutouts
//     (MENU/ROTARY/EXIT) and the BOTTOM-wall USB-C slot, because those are accessed
//     from the edge, in the plane of the wall, not through the face.
// button_extender() — a small printable plunger for MENU/EXIT (see the "Button
//   extenders" section below for why they're needed and how they're installed).
//
// TODO(verify) — ALL dimensions below are placeholders / educated guesses, not
// measurements. Elecrow publishes the real PCB outline as a .stp file in their GitHub
// repo (Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250, "3D file"
// folder) — open it in FreeCAD, read the bounding box, mounting-hole positions, and the
// exact XYZ of MENU/ROTARY/EXIT/USB-C/RESET, and replace the values below before
// printing anything. Only the display's active-area size (31.2 x 63.19mm) and
// resolution came from Elecrow's published spec; the rest — including which edge each
// connector is on — comes from reading the annotated product photo, not a CAD source,
// so positions along each edge are still evenly-spaced guesses even though the edge
// assignment itself should now be correct.
//
// See enclosure/README.md.

// ---- Board (TODO: replace with real values from the .stp file) ----
PCB_WIDTH = 40;      // mm, guess
PCB_LENGTH = 82;     // mm, guess
PCB_THICKNESS = 1.6; // mm, standard
DISPLAY_ACTIVE_W = 31.2; // mm, from Elecrow spec (confirmed)
DISPLAY_ACTIVE_L = 63.19; // mm, from Elecrow spec (confirmed)
// Offset of the display's active area from the PCB's top edge / center — guess. This
// also doubles as the depth of the top margin band that holds GPIO_D/BOOT/RESET.
DISPLAY_OFFSET_Y = 8;

// Mounting holes — TODO: real positions from .stp. Placeholder: 4 corners, 3mm inset.
MOUNT_HOLE_D = 2.2; // for M2 self-tapping screws
MOUNT_INSET = 3;

// ---- Case shell ----
WALL = 1.8;
CLEARANCE = 0.3; // extra room around the PCB so it isn't a press-fit
STANDOFF_H = 4;   // height the PCB sits above the back floor, on screw posts
// Loose depth behind the PCB for the battery to sit in — NOT a dedicated pocket, just
// extra shell depth. A few mm is enough for a common thin LiPo pouch cell (e.g. a
// 502030-class cell is ~5mm thick); the battery is not press-fit or walled in, it just
// rests in whatever flat volume is left behind the standoffs. TODO: confirm against the
// actual cell once one is chosen — this is a depth budget, not a battery outline.
BATTERY_CAVITY_H = 6;

// Approximate Z height (measured from the back floor) of the PCB's edge-mounted
// components — i.e. where MENU/ROTARY/EXIT/USB-C actually sit once the board is resting
// on its standoffs. Guess: standoff height + half the PCB thickness (component height
// above/below the board surface itself is unknown). TODO(verify): replace once the real
// board + connector heights are known; this single number currently drives every
// left/bottom wall cutout's vertical placement.
EDGE_COMPONENT_Z = STANDOFF_H + PCB_THICKNESS / 2;

// ---- Left-edge cutouts: MENU, Rotary Switch, EXIT (top to bottom, per the photo) ----
// Small rectangular slots for the two side-actuated tactile buttons. TODO(verify): real
// spacing/size — these are evenly-spaced guesses along the left wall (fractions of the
// case's total outer length, defined once here so front_frame/back_shell agree).
CASE_LENGTH = PCB_LENGTH + 2 * CLEARANCE + 2 * WALL;
BUTTON_SLOT_W = 3.5; // along the wall (Y axis) — mm
BUTTON_SLOT_H = 3;   // vertical extent (Z axis) — mm
MENU_Y = CASE_LENGTH * 0.18;
ROTARY_Y = CASE_LENGTH * 0.50;
EXIT_Y = CASE_LENGTH * 0.82;

// Rotary encoder cutout needs real clearance for a knob that has to physically rotate —
// this is deliberately a plain round hole, not a button-sized via. TODO(verify): actual
// knob diameter; 9mm is a guess sized for a common small encoder knob with some spin
// clearance.
ROTARY_KNOB_D = 9;

// ---- Bottom-edge cutout: USB-C only ----
// USB-C gets a real slot because it's used routinely (charging + data). UART0 and BAT
// are deliberately left with NO exterior cutout:
//   - UART0 is a debug/flashing console, not a daily-use interface — treat it like
//     BOOT/GPIO_D on the top edge: open the case if you need it.
//   - BAT is a JST pigtail. The expected assembly order is: solder/plug the battery in,
//     tuck it into the flat cavity behind the PCB, THEN close the case — so the battery
//     connector never needs to be mated or unmated through a closed shell. If your build
//     needs battery hot-swap without opening the case, add a cutout here.
// TODO(verify): real USB-C position — currently centered on the bottom wall, which is
// only approximately right per the photo (UART0 left, USB-C center, BAT right).
USBC_W = 9;
USBC_H = 3.2;

// ---- Top-face cutout: RESET pinhole only ----
// BOOT and RESET are ordinary top-mounted tactiles pressed straight down through the
// front face, in the board's top margin band (between the PCB's top edge and the
// display's active area). BOOT and GPIO_D get no dedicated access — both are one-time
// flashing/debug interfaces, and opening the case is a reasonable ask for that.
// RESET gets a small paperclip-sized pinhole because a reader that hangs needs a reset
// without full disassembly. TODO(verify): real X/Y position of RESET within the top
// margin band; this is a guess placed away from the screen cutout and mount holes.
RESET_PIN_D = 1.6;

// ---- Button extenders (MENU / EXIT) ----
// The PCB sits inset from the case's outer wall surface by WALL + CLEARANCE (the wall
// thickness, plus the gap around the board). A side-mounted tactile switch's actual cap
// sits at the PCB edge, i.e. recessed behind the outer wall face by roughly that same
// amount — it will NOT reach flush with the case exterior on its own. These are small
// printable plunger pieces that bridge that gap.
//
// Shape: a flat cap/head (wide, thin — sits against the outer wall face and is what the
// fingertip presses) with a shaft rising from its center (slides through the wall slot
// and rests against the switch's actuator). Printed head-down on the bed, shaft pointing
// up — a squat "mushroom" cross-section, no overhangs beyond the head's edge, so it
// needs no supports.
//
// Assembly: friction-fit, inserted from OUTSIDE through the wall slot before the case is
// closed, head-first is impossible (head is wider than the slot on purpose) — insert
// shaft-first from the inside before mating front_frame/back_shell, so the head ends up
// resting against the outer wall face and the shaft points inward at the switch. The
// shaft is deliberately a shade shorter than WALL + CLEARANCE (see EXTENDER_SHAFT_LEN)
// so it doesn't pre-load the switch at rest; if it ends up too short to reach, add a
// drop of hot glue between the shaft tip and the switch cap rather than reprinting with
// a guessed longer shaft. No glue is needed to retain the piece itself — the head is
// wider than the slot on both faces of travel, so it can't fall out either way.
//
// The rotary encoder does NOT get an extender: encoder shafts/knobs typically already
// protrude further than a tactile button's cap, and the ROTARY_KNOB_D cutout above is
// sized to let the knob itself reach the case exterior directly.
EXTENDER_HEAD_W = BUTTON_SLOT_W + 3; // wider than the slot so it can't slip through
EXTENDER_HEAD_L = BUTTON_SLOT_H + 3;
EXTENDER_HEAD_T = 1.2;
EXTENDER_SHAFT_W = BUTTON_SLOT_W - 0.4; // sliding clearance inside the slot
EXTENDER_SHAFT_H = BUTTON_SLOT_H - 0.4;
EXTENDER_SHAFT_LEN = WALL + CLEARANCE - 0.3; // slightly short of the full gap — see above

module rounded_rect(w, l, r, h) {
  hull() {
    for (x = [r, w - r])
      for (y = [r, l - r])
        translate([x, y, 0]) cylinder(r = r, h = h, $fn = 32);
  }
}

module mount_holes(w, l, h) {
  // 4 corner screw posts — these are the ONLY circular holes that belong near the
  // case perimeter. They are structural (PCB mounting screws), not buttons.
  for (x = [MOUNT_INSET, w - MOUNT_INSET])
    for (y = [MOUNT_INSET, l - MOUNT_INSET])
      translate([x, y, -1]) cylinder(d = MOUNT_HOLE_D, h = h + 2, $fn = 20);
}

// Front frame: bezel over the display face. Only cutouts that are accessed
// perpendicular to the board face belong here — the screen, RESET, and the mount
// screws. MENU/EXIT/ROTARY/USB-C are on back_shell's walls instead (see below).
module front_frame() {
  w = PCB_WIDTH + 2 * CLEARANCE + 2 * WALL;
  l = PCB_LENGTH + 2 * CLEARANCE + 2 * WALL;
  h = 3;

  difference() {
    rounded_rect(w, l, 3, h);

    // screen cutout, centered over the display's active area
    translate([(w - DISPLAY_ACTIVE_W) / 2, DISPLAY_OFFSET_Y + WALL, -1])
      cube([DISPLAY_ACTIVE_W, DISPLAY_ACTIVE_L, h + 2]);

    // RESET pinhole, in the top margin band (between the PCB's top edge and the
    // display cutout) — see the "Top-face cutout" section above for why only RESET
    // gets one.
    translate([w * 0.75, WALL + DISPLAY_OFFSET_Y * 0.5, -1])
      cylinder(d = RESET_PIN_D, h = h + 2, $fn = 12);

    mount_holes(w, l, h);
  }
}

// Back shell: the piece with real wall height. Owns every edge-actuated cutout
// (MENU/ROTARY/EXIT on the left wall, USB-C on the bottom wall) because those switches
// are pressed/turned in the plane of the wall, not through the front face. Also owns
// the PCB standoffs and the flat, unwalled battery cavity behind them.
module back_shell() {
  w = PCB_WIDTH + 2 * CLEARANCE + 2 * WALL;
  l = PCB_LENGTH + 2 * CLEARANCE + 2 * WALL;
  wall_h = WALL + STANDOFF_H + BATTERY_CAVITY_H;
  z = WALL + EDGE_COMPONENT_Z; // absolute Z of edge components within the shell

  difference() {
    rounded_rect(w, l, 3, wall_h);

    // hollow out from the top down to a WALL-thick floor
    translate([WALL, WALL, WALL])
      cube([w - 2 * WALL, l - 2 * WALL, wall_h]);

    // LEFT wall — MENU (top), ROTARY (middle), EXIT (bottom), in that order.
    translate([-1, MENU_Y - BUTTON_SLOT_W / 2, z - BUTTON_SLOT_H / 2])
      cube([WALL + 2, BUTTON_SLOT_W, BUTTON_SLOT_H]);
    translate([-1, ROTARY_Y, z])
      rotate([0, 90, 0])
        cylinder(d = ROTARY_KNOB_D, h = WALL + 2, $fn = 32);
    translate([-1, EXIT_Y - BUTTON_SLOT_W / 2, z - BUTTON_SLOT_H / 2])
      cube([WALL + 2, BUTTON_SLOT_W, BUTTON_SLOT_H]);

    // BOTTOM wall — USB-C slot only (see "Bottom-edge cutout" section above).
    translate([(w - USBC_W) / 2, l - WALL - 1, z - USBC_H / 2])
      cube([USBC_W, WALL + 2, USBC_H]);
  }

  // PCB standoffs (screw posts) sitting inside the shell
  for (x = [MOUNT_INSET, w - MOUNT_INSET])
    for (y = [MOUNT_INSET, l - MOUNT_INSET])
      translate([x, y, WALL])
        difference() {
          cylinder(d = MOUNT_HOLE_D + 3, h = STANDOFF_H, $fn = 20);
          translate([0, 0, -1]) cylinder(d = MOUNT_HOLE_D, h = STANDOFF_H + 2, $fn = 20);
        }

  // No dedicated battery structure: BATTERY_CAVITY_H above is just shell depth left
  // flat and open behind the standoffs. The battery sits loose in it — see the
  // BATTERY_CAVITY_H comment for why there's no pocket/wall here.
}

// Small printable plunger for MENU/EXIT — see the "Button extenders" section above.
module button_extender() {
  union() {
    // head/cap — presses against the outer wall face
    translate([0, 0, 0])
      cube([EXTENDER_HEAD_W, EXTENDER_HEAD_L, EXTENDER_HEAD_T], center = true);
    // shaft — slides through the wall slot to the switch
    translate([0, 0, EXTENDER_HEAD_T / 2 + EXTENDER_SHAFT_LEN / 2])
      cube([EXTENDER_SHAFT_W, EXTENDER_SHAFT_H, EXTENDER_SHAFT_LEN], center = true);
  }
}

// Render both case pieces plus a pair of button extenders (MENU, EXIT) side by side for
// slicing. Comment out translates to inspect a single part.
translate([-60, 0, 0]) front_frame();
translate([20, 0, 0]) back_shell();
translate([-60, -30, 0]) button_extender();
translate([-45, -30, 0]) button_extender();
