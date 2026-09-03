# Where the simulator diverges from real hardware

The browser simulator is an honest preview of the reading experience, not an emulator. These
are the places where it deliberately differs from the device, and what the difference costs.

- **Font.** The screen uses a pixel web font (Silkscreen) measured live via Canvas — not the
  actual u8g2 glyph metrics the firmware draws with, so exact line breaks will shift slightly
  once `tools/dump_font_metrics` runs on real hardware.

- **Italic.** Shown as a synthetic slant for preview purposes. u8g2 has no real italic face at
  this size, so firmware falls back to regular — see [Architecture](ARCHITECTURE.md).

- **Refresh flash.** Modeled on real SSD1680/EPD behavior — full refresh flickers through a
  couple of black/white cycles (the datasheet-level description is "flickers multiple times,"
  vs. once for fast-refresh and never for partial), and partial refresh accumulates faint
  ghosting that only a full refresh clears. But it's a stylized per-pixel approximation, not
  the SSD1680's actual waveform LUTs — real timing and exact ghosting patterns will differ.
  Real full refresh also takes ~2-3s; the simulator speeds this up to stay pleasant to use.

- **Always landscape.** The panel is a 122×250 portrait sensor natively, but that's ~19
  characters/line — bad enough that portrait was never worth shipping as an option. Rendered
  rotated (250×122) unconditionally, matching `display.setRotation(1)` in
  `firmware/src/renderer.cpp`.

- **No battery indicator.** The stock schematic has no battery-voltage sense line at all, so
  the simulator doesn't show one — real hardware reads `-1` (unavailable) until a resistor
  divider is added. See `firmware/src/config.h` / `battery.cpp` for the two-resistor fix; the
  UI can show a real percentage once that's wired up.

- **Resume position, bookmarks and reading stats.** In the browser these live in
  `localStorage`, keyed by title+author — not in the book file. Re-loading the *same* EPUB
  (or the sample) restores where you left off. On device this is simpler: the book is already
  in flash, so `firmware/src/library.cpp` keeps one small `/state.json` record keyed by path,
  written debounced rather than on every page turn (flash write cycles being the one resource
  a per-turn write would actually burn).

- **Reading-stats clock.** The board has no RTC, so the device gets the date from NTP after
  WiFi connects. Anything read before that goes into an "unknown day" bucket instead of being
  filed under a guessed date. The simulator just asks the browser.

- **Layout parameters aren't a runtime setting.** Changing margin/line-height/font-size in the
  simulator re-paginates instantly because it's a design-time tool — the real device can't do
  that. Page breaks are precomputed once, at conversion time, and baked into each `.cebk`
  ([FORMAT.md](FORMAT.md)); the whole point was zero text-layout work on the ESP32. Change a
  constant in `firmware/src/config.h` after books are on the device and their saved page/line
  tables no longer match what gets rendered — every book has to be re-converted and
  re-uploaded. Use the sliders to pick final values, then hardcode them and reflash. The
  on-device Settings screen shows these values but doesn't offer to change them; what it
  *does* offer (full-refresh cadence, wallpaper) is genuinely runtime-safe.

- **Game cursor highlight.** The simulator inverts whatever is under the cursor using canvas
  `difference` compositing. A 1-bit panel can't composite, so the firmware draws the cursor in
  whichever color contrasts with the cell it's on — it always knows the cell's fill state, so
  the result is the same guaranteed-visible highlight, computed instead of composited.

- **Sleep wallpaper.** In the simulator, an image embedded directly in the page and dithered
  to the panel's real 1-bit palette (a real EPD can't show grayscale, so drawing it as a photo
  would misrepresent what this simulates). On device, a wallpaper is a raw 1-bit 122×250
  bitmap in `/wallpapers` — 3,813 bytes regardless of the source photo, see
  [Flash & RAM budget](FLASH_BUDGET.md).

- **Ghosting simulation itself.** A canvas can't physically ghost, so the simulator fakes it.
  Real firmware just calls GxEPD2's partial/full refresh and the physical panel ghosts on its
  own — there's no firmware counterpart to this code at all.

- **Fullscreen, touch, and haptics.** Browser affordances standing in for not having the
  device in hand. The real board has no touchscreen, no vibration motor, and is always
  "fullscreen" — it's a dedicated display, not a browser tab.
