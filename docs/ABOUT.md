# About this project

A DIY e-reader built around the [Elecrow CrowPanel ESP32 2.13" E-Paper HMI
Display](https://www.elecrow.com/crowpanel-esp32-2-13-e-paper-hmi-display-with-122-250-resolution-black-white-color-driven-by-spi-interface.html)
— an ESP32-S3 with a 122×250 black-and-white e-paper panel, a rotary encoder and two
buttons, and no SD slot. EPUBs are converted in the browser to a precomputed binary format
(`docs/FORMAT.md`) and uploaded over WiFi, so the device itself does no text layout at all.

The browser simulator you're reading this from is the design tool for it: same pagination
logic, same input model, an approximation of e-ink refresh behavior.

## Disclosure: this is "vibe coded"

The firmware, converter, simulator, and these docs were written collaboratively with
[Claude](https://claude.com/claude-code) (Anthropic), directed and reviewed by a human,
**without physical access to the target hardware**. Nothing here has run on a real board.
Pin mappings, driver classes and refresh timing are reasoned from Elecrow's published
schematic and datasheets — treat them as needing a multimeter check, not as measured fact.
[Hardware divergences](HARDWARE_DIVERGENCES.md) lists what the simulator fakes and why.

## What the firmware review found

The firmware was reviewed end-to-end and built against a real toolchain (`arduino-cli`
with ESP32 core 3.3.10) rather than only read. That build is what turned several
"looks fine" judgements into facts — the headline one being that **the firmware did not
compile at all** before this pass.

Fixed:

- **A compile error in the display setup.** `renderer.cpp` called a `GxEPD2::init()`
  overload that doesn't exist (five arguments ending in a `SPIClass&`); the bus has to be
  selected separately via `epd2.selectSPI()`. Nothing downstream of this could ever have
  run.
- **Undefined behavior on a truncated or corrupt `.cebk`.** The 16- and 32-bit readers
  returned uninitialized stack bytes when a read came up short, which became wild file
  seeks and multi-megabyte `reserve()` calls. Reads are now checked, and the chapter
  table, page table and text-blob bounds are all validated.
- **A wake-from-sleep bug that would have looked like a dead device.** Light sleep armed
  `ext1` with `ALL_LOW` across all five inputs — meaning every button had to be held down
  simultaneously to wake it. It's `ANY_LOW` now.
- **Dropped button presses.** The input poll returned on the first pressed button, so the
  other buttons' debounce state machines didn't advance that tick. Every button is now
  polled every tick and events are queued.
- **Books that only appeared after a reboot** (the upload callback was an empty `TODO`),
  **a library scan that didn't filter for `.cebk`** or close its file handles, and a path
  construction that broke on ESP32 core versions where `File::name()` returns a full path.
- **The panel never being put to sleep.** `Renderer::powerDown()` existed and was never
  called, so the display rail stayed powered indefinitely.
- **Reading position lost on every reboot** — there was no persistence of any kind.
- **Per-span file I/O inside the draw loop.** Rendering a page did a seek, a heap
  allocation and a `String` construction *per styled span*; it now reads the page's text
  once into a reusable buffer. The style-run scan is no longer O(spans × runs) either.
- **A converter crash on non-ASCII titles.** Titles were truncated by character count but
  length-prefixed by byte count, so a long title in any non-Latin script threw instead of
  being truncated.

The full list, including what was checked and found correct, is in
[Firmware review](FIRMWARE_REVIEW.md).

## Firmware/simulator parity

The firmware used to be a reading skeleton: open the first book found, turn pages, and
nothing else — MENU and EXIT were explicitly unimplemented. The menu system, games,
bookmarks and stats existed only in this simulator.

That gap is closed. The device now has the same screen stack, the same two-layer menu
(MENU opens the current book's menu, EXIT opens Home), the same six gestures, the same
five games, bookmarks, reading stats, idle sleep with a wallpaper, and resume-where-you-
left-off. [Simulator UI/UX](SIMULATOR.md) documents the intended behavior for both.

Two deliberate differences remain, both of them honest ones:

- **Layout parameters aren't adjustable on the device.** Margin, line height and font size
  are baked into every `.cebk` at conversion time, so the on-device Settings screen shows
  them as fixed values instead of offering sliders that would desync the page tables.
- **No battery indicator.** The stock schematic has no battery-sense line at all, so
  `battery::readPercent()` returns `-1` and nothing displays a percentage. Two SMD
  resistors and one `config.h` constant would change that.

## Sources & inspirations

- **Hardware.** [Elecrow CrowPanel ESP32 2.13" E-Paper HMI
  Display](https://www.elecrow.com/crowpanel-esp32-2-13-e-paper-hmi-display-with-122-250-resolution-black-white-color-driven-by-spi-interface.html).
- **Precedent projects** (see [Architecture](ARCHITECTURE.md) for how this one differs):
  [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)
  (equivalent tag-stripping, done on-device) and Soldered's Inkplate 6FLICK (host-side
  Python EPUB pre-conversion). This project follows the second pattern, in the browser,
  with pagination also moved off-device.
- **Firmware libraries.** GxEPD2, U8g2_for_Adafruit_GFX, Adafruit GFX Library,
  WiFiManager, ESPAsyncWebServer/AsyncTCP, ArduinoJson.
- **Converter.** JSZip, for standalone EPUB unzipping outside this simulator.
- **Fonts.** Silkscreen, IBM Plex Mono and IBM Plex Sans (Google Fonts).
- **Sample books.** *Alice's Adventures in Wonderland*, *The Strange Case of Dr. Jekyll and
  Mr. Hyde*, and *The Wonderful Wizard of Oz* — genuinely US-public-domain texts via
  [Project Gutenberg](https://www.gutenberg.org/).

Full source, issues and the README:
[github.com/trimpta/esp32-epaper-ereader](https://github.com/trimpta/esp32-epaper-ereader).
