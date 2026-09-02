// Parametric two-piece case for the CrowPanel ESP32 2.13" E-Paper board.
// Front frame: bezel around the screen + cutouts for MENU/EXIT/rotary + USB-C.
// Back shell: standoffs for the PCB + a separate battery pocket behind it.
//
// TODO(verify) — ALL dimensions below are placeholders / educated guesses, not
// measurements. Elecrow publishes the real PCB outline as a .stp file in their GitHub
// repo (Elecrow-RD/CrowPanel-ESP32-2.13-E-paper-HMI-Display-with-122-250, "3D file"
// folder) — open it in FreeCAD, read the bounding box and mounting-hole positions, and
// replace PCB_WIDTH/PCB_LENGTH/hole positions/button positions below before printing
// anything. Only the display's active-area size (31.2 x 63.19mm) and resolution came
// from Elecrow's published spec; the rest of the board outline is a guess sized to look
// plausible for a ~2.13" HMI dev board.
//
// See enclosure/README.md.

// ---- Board (TODO: replace with real values from the .stp file) ----
PCB_WIDTH = 40;      // mm, guess
PCB_LENGTH = 82;     // mm, guess
PCB_THICKNESS = 1.6; // mm, standard
DISPLAY_ACTIVE_W = 31.2; // mm, from Elecrow spec (confirmed)
DISPLAY_ACTIVE_L = 63.19; // mm, from Elecrow spec (confirmed)
// Offset of the display's active area from the PCB's top edge / center — guess.
DISPLAY_OFFSET_Y = 8;

// Mounting holes — TODO: real positions from .stp. Placeholder: 4 corners, 3mm inset.
MOUNT_HOLE_D = 2.2; // for M2 self-tapping screws
MOUNT_INSET = 3;

// Button row (MENU, EXIT, rotary dial) — TODO: real positions. Placeholder: right edge,
// evenly spaced, matching "back button, home button, and rotary switch" from the
// product description.
BUTTON_HOLE_D = 3.5;
ROTARY_HOLE_D = 6;

// USB-C cutout — TODO: real position/size.
USBC_W = 9;
USBC_H = 3.2;

// ---- Case shell ----
WALL = 1.8;
CLEARANCE = 0.3; // extra room around the PCB so it isn't a press-fit
STANDOFF_H = 4;   // battery pocket depth behind the PCB
BATTERY_W = 20;   // fits a common 502030 LiPo (20 x 30 x 5mm) on its side
BATTERY_L = 32;
BATTERY_H = 6;

module rounded_rect(w, l, r, h) {
  hull() {
    for (x = [r, w - r])
      for (y = [r, l - r])
        translate([x, y, 0]) cylinder(r = r, h = h, $fn = 32);
  }
}

module mount_holes(w, l, h) {
  for (x = [MOUNT_INSET, w - MOUNT_INSET])
    for (y = [MOUNT_INSET, l - MOUNT_INSET])
      translate([x, y, -1]) cylinder(d = MOUNT_HOLE_D, h = h + 2, $fn = 20);
}

// Front frame: thin bezel with a screen cutout and the button/USB-C holes.
module front_frame() {
  w = PCB_WIDTH + 2 * CLEARANCE + 2 * WALL;
  l = PCB_LENGTH + 2 * CLEARANCE + 2 * WALL;
  h = 3;

  difference() {
    rounded_rect(w, l, 3, h);

    // screen cutout, centered over the display's active area
    translate([(w - DISPLAY_ACTIVE_W) / 2, DISPLAY_OFFSET_Y + WALL, -1])
      cube([DISPLAY_ACTIVE_W, DISPLAY_ACTIVE_L, h + 2]);

    // button row along the right edge — TODO: real spacing
    for (i = [0:2])
      translate([w - WALL - 4, l * 0.55 + i * 12, -1])
        cylinder(d = BUTTON_HOLE_D, h = h + 2, $fn = 16);

    // rotary dial
    translate([w - WALL - 4, l * 0.85, -1])
      cylinder(d = ROTARY_HOLE_D, h = h + 2, $fn = 24);

    // USB-C, bottom edge, centered
    translate([(w - USBC_W) / 2, -1, (h - USBC_H) / 2])
      cube([USBC_W, WALL + 2, USBC_H]);

    mount_holes(w, l, h);
  }
}

// Back shell: PCB standoff floor + a separate battery pocket behind it, sized for a
// common small LiPo pouch cell. Swap BATTERY_W/L/H for your actual cell.
module back_shell() {
  w = PCB_WIDTH + 2 * CLEARANCE + 2 * WALL;
  l = PCB_LENGTH + 2 * CLEARANCE + 2 * WALL;
  wall_h = STANDOFF_H + BATTERY_H + WALL;

  difference() {
    union() {
      // outer shell
      rounded_rect(w, l, 3, wall_h);
    }
    // hollow out from the top down to a WALL-thick floor
    translate([WALL, WALL, WALL])
      cube([w - 2 * WALL, l - 2 * WALL, wall_h]);
  }

  // PCB standoffs (screw posts) sitting inside the shell
  for (x = [MOUNT_INSET, w - MOUNT_INSET])
    for (y = [MOUNT_INSET, l - MOUNT_INSET])
      translate([x, y, WALL])
        difference() {
          cylinder(d = MOUNT_HOLE_D + 3, h = STANDOFF_H, $fn = 20);
          translate([0, 0, -1]) cylinder(d = MOUNT_HOLE_D, h = STANDOFF_H + 2, $fn = 20);
        }

  // battery pocket walls, behind the PCB plane (i.e. deeper in Z), roughly centered
  translate([(w - BATTERY_W) / 2, (l - BATTERY_L) / 2, WALL + STANDOFF_H])
    difference() {
      cube([BATTERY_W + 2 * WALL, BATTERY_L + 2 * WALL, BATTERY_H + WALL]);
      translate([WALL, WALL, WALL])
        cube([BATTERY_W, BATTERY_L, BATTERY_H + 1]);
    }
}

// Render both parts side by side for slicing. Comment one out to inspect individually.
translate([-60, 0, 0]) front_frame();
translate([20, 0, 0]) back_shell();
