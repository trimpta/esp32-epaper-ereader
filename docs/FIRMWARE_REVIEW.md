# Firmware review

A full read-through of `firmware/src/`, checked against a real toolchain rather than by
inspection alone, plus a parity pass against what the simulator actually does.

## How it was verified

There's still no physical board. What changed is that the firmware is now **compiled**:
`arduino-cli` with `esp32:esp32` core 3.3.10, targeting `esp32:esp32:esp32s3`, against the
same libraries `platformio.ini` pins (GxEPD2, U8g2_for_Adafruit_GFX, Adafruit GFX,
WiFiManager, an ESPAsyncWebServer/AsyncTCP pair, ArduinoJson).

That distinction matters: the single most serious bug below is one no amount of careful
reading had caught, and one that a build catches immediately.

What a compile still can't tell you: whether `GxEPD2_213_BN` is the right driver class for
this panel, whether the SPI/EPD pin numbers read off the schematic are correct, whether the
`ext1` wake pin set is valid on this board, or what a refresh actually looks like. Those
remain hardware-verification items — see the README's status section.

## Bugs found and fixed

### 1. The firmware did not compile

`renderer.cpp` called `display.init(115200, true, 20, false, SPI)`. GxEPD2 has no `init()`
overload taking a bare `SPIClass&` — the SPI bus is selected separately:

```cpp
SPI.begin(PIN_EPD_SCK, -1, PIN_EPD_MOSI, PIN_EPD_CS);
display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
display.init(115200, true, 20, false);
```

Everything else in this document is downstream of a binary that could never have been built.

### 2. Undefined behavior on a corrupt or truncated `.cebk`

`readU16()`/`readU32()` declared a local byte array, passed it to `File::read()`, and
returned it **without checking how many bytes were actually read**. At EOF that's whatever
was on the stack. Those values are file offsets and element counts, so a truncated book
produced arbitrary seeks and `reserve()` calls sized by garbage.

Three related holes, all now closed:

- `chapters_.reserve(chapterCount)` trusted a `uint16_t` from the header — up to 65,535
  entries — with no sanity check against the file's actual size.
- `getPageLines()` computed `endLine - startLine` in unsigned arithmetic and passed it to
  `reserve()`. A page table where `endLine < startLine` asked for a multi-gigabyte
  allocation.
- `readText()` never bounds-checked against the text blob, even though the blob's length is
  right there in the header (it was read and discarded). Past the end, it rendered whatever
  bytes followed in the file.

The reader now zero-fills, tracks short reads in a `readOk_` flag, validates every table
offset against the file size, and refuses to open a book that fails those checks.

### 3. Light sleep could only be woken by pressing every button at once

`esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ALL_LOW)` with all five inputs in the
mask means "wake when **all** of these are low simultaneously." The buttons are active-low
and independent, so the intent was clearly any-of-them. On a board where this path was
enabled, that reads as a device that goes to sleep and never comes back.

Now `ESP_EXT1_WAKEUP_ANY_LOW`, with each pin checked via
`esp_sleep_is_valid_wakeup_gpio()` first and a `false` return so the caller can fall back to
polling rather than sleeping forever.

### 4. Dropped button presses

`input::poll()` returned as soon as it found one pressed button, so the remaining buttons'
debounce state machines didn't advance on that tick. Every button is now polled every tick,
with events queued in a small ring buffer.

### 5. Only three of the six gestures existed

The simulator's input model is short *and* long press on MENU, EXIT and the rotary's click.
Firmware had no long-press detection at all, which meant no force-full-refresh, no
sleep-now, and no scrub-to-edit. All six now exist, at the same 550 ms threshold the
simulator uses.

### 6. Library handling

`openFirstBook()` opened whatever `openNextFile()` returned first: no `.cebk` filter (a
half-finished upload would be picked instead of a book), no directory check, no `close()`,
and `String(BOOKS_DIR) + "/" + f.name()` — which produces `/books//books/x.cebk` on ESP32
core versions where `File::name()` returns a full path. Replaced by `library.cpp`, which
scans properly, reads each book's header for title/author/page count, and skips anything
that fails to parse.

### 7. The panel was never put to sleep

`Renderer::powerDown()` (a `display.hibernate()` wrapper) existed and was never called, and
`PIN_EPD_POWER_CTL` was driven high at boot and left there. The display now hibernates when
the reader goes to sleep.

### 8. No persistence

Nothing was saved. Every reboot reopened the first book at page one. The device now keeps
resume position, bookmarks and reading stats in `/state.json`, written debounced (on sleep,
and at most once per 30s of quiet) rather than on every page turn, and via a
write-temp-then-rename so a power cut mid-write can't destroy the whole library's state.

### 9. Uploads needed a reboot

`web_server::begin()`'s "a book finished uploading" callback was an empty `TODO`. It now
triggers a library rescan.

### 10. Converter: non-ASCII titles threw

`converter.js` truncated titles with `slice(0, 255)` — 255 **characters** — then
length-prefixed them with a byte count that must fit in a `uint8_t`. Any title long enough
in a non-Latin script exceeded 255 bytes and threw, aborting the conversion. Chapter titles
weren't truncated at all. Both now truncate on a UTF-8 byte boundary.

## Efficiency

- **Page rendering did file I/O per styled span.** Each span meant a `seek()`, a
  `std::vector` allocation and a `String` construction, inside the draw loop — dozens of
  small reads per page turn. A page's text is now read once into a reusable static buffer
  and printed straight from it.
- **Style-run lookup was O(spans × runs).** Every span rescanned the chapter's entire run
  table. Runs are sorted, and lines are drawn in order, so a monotonic cursor now carries
  the scan position forward.
- **`loop()` still polls at 20 ms.** The idle-sleep path now hibernates the panel and stops
  drawing, but the CPU-side light-sleep handoff stays behind the unverified-`ext1` TODO —
  turning it on before the wake pins are confirmed is exactly the "sleeps and never wakes"
  failure mode above.

## Checked and found correct

Worth recording, so the next pass doesn't re-derive it:

- **The `.cebk` reader matches the writer**, field for field: magic, version, u8-length
  title/author, `u16` chapter count, then per chapter title / `u32` text offset / `u32`
  text length / `u16` line count + 6-byte line entries / `u16` page count + `u16` page
  starts / `u16` run count + 7-byte runs, then `u32` blob length and the blob.
  `converter.js`'s writer and `book_format.cpp`'s reader agree exactly, and both match
  [FORMAT.md](FORMAT.md).
- **The renderer's line-advance model matches the simulator's**, including the detail that
  both place the first baseline at `margin + LINE_HEIGHT` even when the first line is a
  heading. That's one pixel-row tighter than the paginator's box model assumes for a page
  that opens on a heading — it can only ever draw *less* tall than budgeted, so it can't
  overflow a page, and firmware and simulator are consistent with each other, which is what
  matters for previewing.
- **The layout constants agree across all three sides.** `converter/converter.js`'s `LAYOUT`
  and `firmware/src/config.h` both say 250×122, margin 4, line height 11, heading height
  14 — which they must, since the converter bakes page breaks computed from those numbers
  into each `.cebk` and the firmware draws with them.
- **The font table agrees between firmware and the metrics tool.**
  `renderer.cpp`'s `fontForFlags()` and `tools/dump_font_metrics` both use `helvR08`
  (regular *and* italic, which has no face at this size), `helvB08` (bold), `helvB12` (h1)
  and `helvB10` (h2). If those drifted, the converter would paginate against widths the
  panel never draws.
- **`battery::readPercent()` is correctly gated.** It returns `-1` while `PIN_BATTERY_ADC`
  is `-1`, and `main.cpp` only appends a percentage when the reading is non-negative — so
  the "no battery indicator" decision holds on both sides.
- **The WiFi reconnect backoff** doubles to a 60s ceiling and never re-enters the captive
  portal on its own, which is the right behavior for a phone hotspot that sleeps.

## Parity with the simulator

Before this pass, `main.cpp` said it plainly: *"Reserved for book list / TOC / settings
screens — not implemented in this skeleton."* MENU and EXIT did nothing. The device could
open the first book it found and turn pages; everything else — the menu system, library,
bookmarks, stats, games, sleep screen — existed only in the browser.

Now on device, matching [SIMULATOR.md](SIMULATOR.md):

| Feature | Simulator | Firmware |
|---|---|---|
| Screen stack + two-layer menu (MENU → book menu, EXIT → Home) | yes | yes |
| Six gestures (short/long on MENU, EXIT, rotary click) | yes | yes |
| Library / book menu / chapters / bookmarks | yes | yes |
| Reading stats (pages today, pages/day, est. finish, time read) | yes | yes |
| Settings | layout sliders (design-time) | runtime-safe values only |
| Wi-Fi screen incl. re-entering AP setup | yes | yes |
| Games (Lights Out, Minesweeper, Sudoku, Hangman, Tic-Tac-Toe) | yes | yes |
| Idle sleep + wallpaper sleep screen | yes | yes |
| Resume position, bookmarks persisted | localStorage | `/state.json` |
| Ghosting simulation, fullscreen, touch, haptics | yes | not applicable |

The remaining differences are deliberate and documented in
[Hardware divergences](HARDWARE_DIVERGENCES.md): layout parameters are compile-time on the
device, there's no battery indicator on either side, and the game cursor highlight is
computed rather than composited.

## Still unverified

Everything that needs the physical board, unchanged by this pass:

- `GxEPD2_213_BN` as the driver class for this panel's SSD1680Z/JD79661 controller.
- The EPD SPI pin map and the `IO7_LCD_3.3_CTL` rail-gating pin.
- Whether the `ext1` wake pin set is valid, which gates real power saving.
- Real refresh timing and ghosting behavior.
- Actual font metrics (`tools/dump_font_metrics`), which decide whether the converter's
  line breaks land where the panel draws them.
- Firmware binary size against the 2 MB app partition ([FLASH_BUDGET.md](FLASH_BUDGET.md)).
