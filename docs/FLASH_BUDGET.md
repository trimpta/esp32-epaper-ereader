# Flash & RAM budget

Answering two questions directly: **will everything built in the simulator actually run
on the ESP32-S3**, and **how much flash is left for books once firmware, wallpapers,
and the filesystem itself are accounted for**.

## RAM: not the constraint

ESP32-S3 has 512KB of internal SRAM plus the 8MB of external PSRAM this board wires up
(per Elecrow's spec sheet). Everything built in the simulator this session — the
menu/screen-stack system, five games (Lights Out, Minesweeper, Sudoku, Tic-Tac-Toe,
Hangman), library/bookmarks/resume state, a dithered Voronoi background, ghosting
simulation — measures in the tens of KB at most:

- Largest game state (Sudoku: an 81-cell grid + a given-mask, both tiny arrays) is
  under 1KB.
- Tic-Tac-Toe's minimax explores at most 9! nodes, in practice far fewer — sub-
  millisecond on a 240MHz core, let alone what it'd be in native C++ vs. the browser's
  JS engine.
- The Sudoku generator's backtracking fill is the heaviest single computation here, and
  it's still a one-shot 81-cell search — negligible.
- The actual e-ink framebuffer (GxEPD2, native 122×250 1-bit) is ~3.8KB. Even holding
  two (for partial-refresh double-buffering) is nothing against 512KB, let alone 8MB of
  PSRAM.

None of this requires PSRAM at all — internal SRAM alone covers it with enormous
headroom. **RAM was never the risk on this board; flash is, because there's no SD slot.**

## What doesn't need to be ported at all

Several things built for the simulator exist *because* it's a browser standing in for
hardware, not because real firmware needs an equivalent:

- **Ghosting/multi-flash simulation** (`commitFrame`, `applyGhosting`, `flashSequence`)
  — a canvas can't physically ghost, so the simulator fakes it. Real firmware just
  calls GxEPD2's partial/full refresh and the *physical panel* ghosts on its own. This
  is pure simulator scaffolding with zero firmware counterpart to write.
- **Fullscreen API, touch swipe, haptics (Vibration API), the rotate-to-landscape
  prompt** — all stand-ins for not having the physical device in hand yet. The real
  board has no touchscreen, no vibration motor, and is always "fullscreen" (it's a
  dedicated display, not a browser tab).
- **`localStorage` for bookmarks/resume position** — firmware equivalent is a small
  file per book in LittleFS (or NVS/Preferences for something this small), not a
  behavior that needs porting so much as a storage backend that needs picking.
- **Local-EPUB auto-load via directory listing** — a dev-server convenience; real
  firmware just lists whatever's already in `/books`.

What's left after subtracting all of that — the menu/input state machine, the games'
logic, the library data model — is ordinary C++ business logic. None of it is CPU- or
RAM-heavy enough to be a real porting risk.

## Flash: the actual constraint, and a concrete partition table

The earlier `platformio.ini` referenced `default_8MB.csv` on the assumption that
PlatformIO's espressif32 platform ships something reasonable — that was never actually
verified, so it wasn't a fact this doc could build on. `firmware/partitions.csv` now
defines the layout explicitly instead:

| Partition  | Offset    | Size      | Notes |
|---|---|---|---|
| (bootloader + partition table) | `0x0000` | 36,864 B (36KB) | Fixed ESP32 region before any partition |
| `nvs`      | `0x9000`  | 24,576 B (24KB) | WiFi credentials (WiFiManager), Preferences |
| `phy_init` | `0xF000`  | 4,096 B (4KB)   | RF calibration data |
| `factory`  | `0x10000` | 2,097,152 B (2.00 MiB) | The firmware binary — see estimate below |
| `littlefs` | `0x210000`| 6,225,920 B (5.94 MiB) | Books + wallpapers |

Verified this sums to exactly 8MB (`0x210000 + 0x5F0000 = 0x800000`), not by inspection
but by adding it up in `node` and checking — see the arithmetic isn't just eyeballed.
No `otadata`/dual-app partitions: there's no OTA-over-WiFi flow implemented, so that
space goes to LittleFS instead, which matters more on a board with no SD slot.

### The 2MB app partition is an estimate, not a measurement

Nothing here has actually been compiled — no ESP32 toolchain is available in this
environment. The 2MB figure is reasoned from typical, commonly-reported sizes for
comparable Arduino-ESP32 sketches, not a real build's size report:

| Component | Typical contribution |
|---|---|
| Base Arduino-ESP32 sketch (no WiFi) | ~250–300KB |
| + WiFi/LWIP/mbedTLS (needed by WiFiManager, AsyncWebServer) | +~400–550KB |
| + ESPAsyncWebServer + AsyncTCP | +~50–150KB |
| + WiFiManager (captive portal, its own mini web server) | +~150–300KB |
| + GxEPD2 + U8g2_for_Adafruit_GFX (only the ~5 fonts actually referenced get linked, not the whole font library) | +~40–70KB |
| + ArduinoJson, our own application code | +~30–50KB |
| **Estimated total** | **~1.2–1.5MB** |

2MB leaves ~500KB–800KB of headroom above that estimate — enough margin to not worry
about it, without reserving so much that it eats into book storage for no reason.
**This needs confirming against a real `pio run` size report** once real hardware (or
at least a working toolchain) is available; if it comes in meaningfully over 2MB,
`partitions.csv` is a one-line edit, not a redesign.

## Wallpapers: cheap, by design

A wallpaper is stored as a raw 1-bit bitmap at the panel's native 122×250 resolution —
**not** the source photo. That's `122 × 250 / 8 = 3,813 bytes` (~3.7KB) per wallpaper,
regardless of how large or colorful the original image was, because the dithering that
turns it into that bitmap happens once, client-side, in the browser (or the web panel
upload flow), the same way EPUB conversion already offloads the expensive one-time work
off the device — see `docs/ARCHITECTURE.md`. Even a generous 20 wallpapers is ~76KB —
noise against a multi-MB budget. This is the reason the simulator never embeds a
wallpaper's *source* file into anything meant to represent firmware storage; only the
tiny dithered bitmap corresponds to what real flash usage looks like.

## Books: how many actually fit

Using the three built-in sample books (real EPUBs run through the actual conversion
pipeline, not placeholder text) as a size reference:

| Book | Extracted text | Estimated `.cebk` size |
|---|---|---|
| Alice's Adventures in Wonderland | 162KB | ~186KB |
| The Strange Case of Dr. Jekyll and Mr. Hyde | 157KB | ~180KB |
| The Wonderful Wizard of Oz | 226KB | ~259KB |
| **Average** | | **~208KB** |

(`.cebk` size = text bytes + structural overhead from `docs/FORMAT.md`'s line/page/run
tables — estimated at ~38 chars/line and ~9 lines/page for the landscape layout, and
~5% of lines carrying a bold/italic/heading run. Reasoned the same way as the app-size
estimate above: methodology stated, not measured from a real conversion run of these
exact books through `converter.js` — worth doing once there's reason to, but the order
of magnitude is what matters here.)

LittleFS itself has a small bookkeeping overhead (metadata blocks, wear-leveling
margin) — call it ~95% of the raw partition usable for actual file content, which is a
reasonable assumption for a filesystem holding a few dozen largeish files rather than
thousands of tiny ones:

```
5.94 MiB partition × 0.95 ≈ 5.64 MiB usable
5.64 MiB / ~208KB average book ≈ ~27–28 books this length
```

**That's the headline number: roughly two to three dozen novella-length books, fewer if
they run longer** (a full unabridged doorstop like *Moby-Dick* or *War and Peace* could
be 1–1.5MB+ on its own), more if they're shorter. A handful of wallpapers barely moves
this number at all — books are the entire story here, exactly because there's no SD
card to fall back on.

## Bottom line

- **RAM/CPU: not a risk.** Everything simulated this session fits ESP32-S3 with room
  to spare, and several simulator-only systems (ghosting, fullscreen, haptics) don't
  even need porting.
- **Flash: the real constraint, now with a concrete number.** ~5.9MB for LittleFS,
  ~27 books at this length, wallpapers costing next to nothing. The one figure that
  still needs a real build to confirm is firmware binary size — everything downstream
  of that (this whole book-count estimate) shifts a little if it does, but not by an
  order of magnitude.
