# Simulator UI/UX reference

`simulator/index.html` is a single, self-contained browser page that mimics the on-device reading
experience: same pagination logic as `converter/`, the same three-input model as the real board
(rotary + MENU + EXIT), and an approximation of e-ink's partial-refresh/ghosting behavior. This
doc is the complete reference for how it behaves — what maps to real firmware and what's
simulator-only scaffolding. See also the in-app "Where this diverges from real hardware" panel,
which covers the same divergences in shorter form, and [docs/ARCHITECTURE.md](ARCHITECTURE.md) for
the underlying pipeline this UI sits on top of.

## Input model

Real hardware has exactly three physical inputs: a rotary encoder (scroll + its own push-click)
and two buttons, MENU and EXIT. Each of the three supports a **short** and a **long** press/click,
giving six distinct gestures — all six are wired up:

| Input | Short | Long (hold) |
|---|---|---|
| Rotary scroll | move cursor / turn page (`doNext`/`doPrev`) | — (continuous, not a press) |
| Rotary click (CONF) | context action (`doConf`) — Minesweeper flag, Sudoku cycle-down, otherwise same as MENU | enter scrub **edit mode** on the focused numeric field (`tryEnterEditMode`) |
| MENU | `doMenu()` — see [the two-layer menu](#the-two-layer-menu) below | force a full refresh right now, bypassing the refresh-cadence counter |
| EXIT | `doExit()` — back out one screen (or open Home from the reading view) | put the device to sleep immediately |

In the browser, MENU is left-click, EXIT is right-click, and the rotary dial is its own click
target; all three are wrapped by a generic `bindHoldGesture(el, {onShort, onLong, holdMs})` helper
(Pointer Events, so mouse and touch share one code path) that fires `onShort` on release-before-the-hold-threshold
and `onLong` once the hold threshold is crossed. Scroll on the device mockup (wheel or a
touch drag) maps to rotary scroll. Every input funnels through `handleInput(kind)`, which resets
the idle-sleep timer first, then — if the device is asleep — treats the input as *only* a wake
signal, matching how a real e-reader doesn't act on the same button press that woke it.

The rotary's click (`ROTARY_CONF`) is a genuine third input confirmed against the schematic
(component `TM_2024A`, pins `IO4_DOWN`/`IO6_UP`/`IO5_CONF`) — not a simulator invention. It's a
harmless alternate "select" on plain list/menu screens (same as MENU), and only diverges from
MENU in Minesweeper (flag a cell) and Sudoku (cycle a digit downward instead of up).

## Screen hierarchy

Screens live on a stack (`state.screenStack`), pushed with `pushScreen(name)` and popped with
`popScreen()` — EXIT is generically "pop the stack" except from the reading view, which is
special-cased (see below). Eight of the screens are plain scrollable lists (`LIST_SCREENS`), each
just a title and a row-generator function; a shared renderer (`screenBlocks`/`selectableBlocks`/
`drawBlockList`) handles multi-line wrapped rows, separators, and scroll-into-view windowing so
list screens don't each reimplement scrolling.

```
reading ──MENU──▶ bookMenu ──"Browse chapters"──▶ bookChapters
   │                  │
   │                  ├──"Bookmarks (N)"──▶ bookmarks
   │                  └──"Reading stats"──▶ stats
   │
   └──EXIT──▶ home ──▶ library ──(pick a book)──▶ bookMenu
                  │
                  ├──▶ games ──▶ lightsout / minesweeper / sudoku / hangman / tictactoe
                  ├──▶ settings
                  └──▶ wifi
```

- **home** — Continue-reading shortcut (if a book is open) + Bookmark-this-page, then Library,
  Games, Settings, Wi-Fi.
- **library** — one block per book (wrapped title + author/chapter-count line, dimmed), with a
  separator between books. Replaces an earlier flat "every chapter of every book" list. Picking a
  book always goes to **bookMenu** rather than straight into reading, since a book with existing
  progress needs to ask continue-vs-restart either way.
- **bookMenu** — context menu for one specific book. Shape depends on whether it's the book
  currently open (`state.browseBookIdx === state.activeBook`): the open book gets a quick
  "Bookmark this page" row instead of Continue/Start (you're already there), everything else gets
  Continue (if it has saved progress) or Start reading, plus Browse chapters, Bookmarks, Reading
  stats, and a "More… (Library, Settings, Wi-Fi, Games)" escape hatch to Home.
- **bookChapters** — flat chapter list for the book in `state.browseBookIdx`; picking one jumps
  straight to page 1 of that chapter.
- **bookmarks** — per-book bookmark list; picking one jumps to that saved position.
- **stats** — read-only pace readout for the book in `state.browseBookIdx` (see
  [Reading stats](#reading-stats) below); every row is a display line, not a selectable action.
- **settings** — layout sliders (font size, line height, heading height, margin, full-refresh
  cadence) plus the wallpaper switcher, all scrub-editable (see below).
- **wifi** — a fake connection-state screen (`state.wifiMode`), illustrating AP-mode-vs-connected
  copy; there's no real network code in the simulator.
- **games** — Lights Out, Minesweeper, Sudoku, Hangman, Tic-Tac-Toe. Each game is its own
  self-contained screen outside `LIST_SCREENS` with its own draw/input handlers, but shares the
  rotary-cursor/MENU-select/EXIT-leave pattern with everything else.

### The two-layer menu

MENU and EXIT deliberately open two *different* menus from the reading view:

- **MENU → `bookMenu`** — the current book's own context menu (chapters, bookmarks, bookmark this
  page). This is the fix for a specific friction: jumping to another chapter used to mean
  MENU → Home → Library → re-find this same book → bookMenu → Browse chapters. Now it's one press.
- **EXIT → `home`** — the general menu (Library, Games, Settings, Wi-Fi), for when you actually
  want to leave the book rather than jump around inside it.

`state.browseBookIdx` is what a book-context screen (`bookMenu`, `bookChapters`, `bookmarks`)
actually reads; `doMenu()`'s `reading` case sets `browseBookIdx = activeBook` before pushing, so
those screens don't need a separate "is this the currently-open book" code path from the Library
entry point — only `bookMenuRows()`'s Continue-vs-Bookmark branch cares about the distinction, via
`browseBookIdx === activeBook`.

### Scrub-to-edit numeric settings

Long-pressing the rotary on a scrubbable row (any Settings slider, or the focused cell in Sudoku)
enters an edit-mode session (`state.editMode = {original, get, set, min, max, step}`):

- Rotary scroll adjusts the value directly (dial it in), instead of moving a list cursor.
- **MENU** commits the change and exits edit mode.
- **EXIT** reverts to the original value and exits edit mode.
- The status strip switches to `EDITING (value) · scroll to adjust · MENU saves · EXIT cancels`.

This coexists with the original press-to-cycle behavior — pressing MENU/CONF on a settings row
outside edit mode still steps the value by one increment (wrapping at min/max) the same way it
always did. A row only offers edit mode if it exposes a backing `.el` (the `<input type="range">`
element); the Wallpaper row deliberately has no `.el` (it cycles a name, not a number) and so is
correctly excluded from scrub editing — `tryEnterEditMode()` uses `.el`'s presence as exactly that
signal, nothing more elaborate.

## Refresh & ghosting simulation

E-ink doesn't repaint like an LCD, so the simulator models two distinct refresh types:

- **Partial refresh** (most page turns) is instant and flicker-free, but each one composites a
  faint darkening of whatever was inked in the *previous* frame onto the new one
  (`applyGhosting`) — this is what accumulates as visible ghosting the longer you go between full
  refreshes.
- **Full refresh** runs a short invert/normal/invert/normal flash sequence (`flashSequence`) before
  drawing the new content, clearing all accumulated ghosting. It happens automatically every N
  page turns (the Settings "Full refresh every" slider, `state.layout.fullRefreshEvery`), tracked
  by a counter rather than a timer so it's tied to actual reading activity, not wall-clock time.
- **Long-press MENU** forces a full refresh immediately, bypassing the cadence counter — for when
  ghosting has built up and you don't want to wait for the next automatic one.

This is a stylized per-pixel approximation (luminance blending), not the SSD1680's real waveform
LUTs — exact timing and ghosting patterns will differ on real hardware, and real full refresh takes
roughly 2-3 seconds (sped up here to stay pleasant to use in a browser).

## Reading stats

`bookMenu`'s "Reading stats" row opens a per-book pace readout: progress (pages read / total,
with a percentage), pages read today, an average pages/day figure, an estimated days-to-finish,
and cumulative time spent reading that book. All of it is derived from data the simulator was
already tracking (page position, a book's total page count) plus one new small log
(`readingLogFor`/`bookReadingStats` in `simulator/index.html`): one `{pages, seconds}` bucket per
calendar day per book, in its own `localStorage` key (see [Persistence](#persistence)).

- **Pages** only increments on a forward page turn while that book is the one open and on
  screen (`logPageTurn`, called from `doNext()`) — paging backward to re-read something doesn't
  count as progress, so it can't be inflated by flipping back and forth.
- **Seconds** accumulates from a 5-second heartbeat that only counts time while a book is
  actually on screen, awake, and the tab is visible (`document.visibilityState`) — not while
  asleep, not while browsing menus or playing a game with a book still "open" underneath. Each
  tick is capped at 2× the interval so a laptop suspending mid-session with the tab left open
  can't log an hour of "reading" in one jump.
- **Average pages/day** divides total logged pages by the number of *days with any logged
  activity*, not calendar days since the book was first opened — taking a week off doesn't punish
  the average, it just doesn't move it. Estimated days-to-finish divides remaining pages by that
  average; both read as "not enough data yet" until at least one day has been logged.
- The log is pruned to the most recent 120 days per book on write, so it can't grow unbounded in
  `localStorage`.

Like the rest of the menu system, this exists only in the simulator for now — see
[What's simulator-only](#whats-simulator-only-nothing-to-port-to-firmware). Porting it to firmware
would mean a tiny fixed-size per-book record (today's date, running page/second totals for a
handful of recent days) rather than firmware growing an unbounded log — LittleFS write cycles are
a real constraint the browser's `localStorage` doesn't share.

## Wallpapers

Shown only on the sleep screen. `BUILTIN_WALLPAPERS` ships one built-in image (`lighthouse`,
sourced from `assets/wallpapers/lighthouse.webp` and embedded as a data URI); the web panel's
"Add wallpaper…" upload lets you add more at runtime. Either way, the *source* image is dithered
to the panel's real 1-bit palette client-side (Bayer 4×4 ordered dithering, `ditherImageToPanelCanvas`)
before it's ever treated as a real wallpaper, and cached per-id (`wallpaperCache`) — the simulator
only ever treats the resulting dithered bitmap as "what reaches the device," discarding the
original source once you confirm an upload, because that's the honest cost model: a real panel
can't show grayscale, and the actual firmware would only ever store the finished 1-bit bitmap
(122×250/8 = 3,813 bytes, regardless of how large or colorful the source photo was — see
[docs/FLASH_BUDGET.md](FLASH_BUDGET.md#wallpapers-cheap-by-design)). The in-device Settings screen
cycles between whichever wallpapers are currently available; the web panel additionally shows a
grid of thumbnails to pick from directly.

### Adjustable dithering on upload

A fixed threshold doesn't dither every photo well — a dim photo goes mostly black, an overexposed
one goes mostly white, and a photo's interesting part isn't always dead center. So uploading an
image doesn't commit it immediately: it opens a live preview (`wallpaperPreview`) with **Fit**,
**Pan X/Y**, **Brightness**, **Contrast**, and **Invert** controls, all re-dithering the same
source image in place (`renderWallpaperPreview` → `ditherImageToPanelCanvas`) so you can see the
actual 1-bit result while you adjust, not a grayscale approximation of it.

- **Fit** picks how the source image maps onto the panel's 250×122 frame: **Cover** (scale to
  fill, cropping whatever overflows — the default), **Contain** (scale to fit entirely inside the
  frame, letterboxed with blank space), or **Stretch** (fill exactly, ignoring aspect ratio).
- **Pan X/Y** slide the crop/letterbox window within the frame instead of always centering it —
  meaningful for Cover (which part of a wider/taller source gets kept) and Contain (where the
  letterboxed image sits); a no-op for Stretch, which has no leftover space to slide (the pan
  sliders disable themselves in that mode).

Only once you click "Use this wallpaper" does the currently-previewed dithered bitmap get
committed to `wallpaperCache` and the source image get discarded — clicking Cancel discards the
source without ever exposing it to `wallpaperCache` at all. These settings are per-upload, not
global: each wallpaper keeps whatever bitmap you confirmed for it, not a live reference back to
fit/pan/brightness/contrast values.

## Memory panel

The sidebar's Memory panel computes real `.cebk`-equivalent byte counts from each *currently
loaded* book's actual in-memory `lines`/`pageStarts`/`runs` arrays (per the layout in
[docs/FORMAT.md](FORMAT.md)) plus each wallpaper's fixed 3,813-byte bitmap cost, and compares the
total against the real `firmware/partitions.csv` LittleFS budget. This is a live measurement of
what's actually loaded in the simulator right now, not the FLASH_BUDGET.md doc's necessarily
approximate chars-per-line estimate (that doc reasons about *typical* books; this panel measures
the *specific* books you've loaded).

## True-size calibration

The "📏 True size" toggle overlays a millimeter-marked SVG ruler and a scale slider
(`rulerScale`, persisted to `localStorage` under `rotaryReaderRulerScale_v1`). CSS pixels have no
reliable physical-size meaning across devices/browsers, so instead of trusting an assumed DPI, you
place a real ruler against the screen and adjust the slider until the on-screen ruler matches it —
that gives an accurate px-per-mm conversion for your specific screen at its current zoom level.

By default that calibration only resizes the inert preview box in the calibration panel itself
(labeled with the panel's real 63.2×31.2mm active area). Checking **"Also resize the simulator's
actual display above to true size"** (`cTrueSizeDisplay`, persisted under
`rotaryReaderTrueSizeDisplay_v1`) applies the same calibrated size to the real, interactive
`#screen` canvas instead — overriding its normal "fill the sidebar column" CSS width via an inline
style (`applyTrueSizeDisplay`) so the whole reading/games/menu UI can actually be used at true
physical size, not just looked at in a same-size-as-nothing-else preview rectangle. This is
usually a *smaller* canvas than the default mockup size on typical screens, since 63.2mm is
genuinely small.

## Games

All five (Lights Out, Minesweeper, Sudoku, Hangman, Tic-Tac-Toe) reuse the same input pattern as
everything else — rotary moves a cursor, MENU acts on the focused cell/letter, EXIT leaves — rather
than introducing game-specific controls. Two notable details:

- **Cursor visibility.** Early versions drew the cursor as a same-color outline, which vanished
  against same-colored content (worst case: an "on" cell in Lights Out). All four grid-based games
  now use `invertCellHighlight()` (`ctx.globalCompositeOperation = 'difference'`), which inverts
  whatever's underneath — guaranteed visible regardless of the cell's own color.
- **Tic-Tac-Toe's AI is real minimax**, not a heuristic — "vs AI" means the best a perfect game
  gets you is a draw. The 3×3 board keeps the search space trivially small (well under a
  millisecond), so there's no reason to cut the corner with a weaker opponent.

## Fullscreen, mobile, and touch

The "⤢ Fullscreen" button fullscreens the device mockup only (not the whole page — the sidebar
panels have nothing to do once you're just looking at the screen), and best-effort locks screen
orientation to landscape where the browser supports it (mainly Android Chrome; silently a no-op
elsewhere). On touch devices, a rotate-to-landscape prompt is the reliable fallback for the same
goal. `bindHoldGesture` unifies mouse and touch input through Pointer Events, so every gesture
(short/long press) works identically with a mouse or a finger. Haptic feedback
(`navigator.vibrate`) fires on every input on browsers that support it (Android Chrome-family
only — iOS Safari and desktop browsers no-op harmlessly).

## Sleep & idle timing

`resetIdleTimer()` runs on every input and schedules `goToSleep()` after an idle period that
depends on context: 5 minutes while actively reading (a page sitting on screen while you're still
reading it is a weak idle signal), 1 minute anywhere else (menus, games, settings — browsing a list
untouched is a much stronger idle signal). Any input wakes the device (`wake()`), which redraws
the current screen and flashes once, consuming that same input as "just a wake" rather than also
acting on it. Long-press EXIT sleeps immediately, bypassing the timer.

## Book loading: local EPUB vs. built-in library

On boot, the simulator first tries `tryAutoLoadLocalEpub()` — a dev-server convenience that looks
for an EPUB file sitting next to `index.html` (via a directory listing fetch) and auto-loads it if
found, landing straight in that book. This only works when actually serving the folder locally
(`python -m http.server`, per the README); the published Artifact/GitHub Pages deployment has no
filesystem to list, so it always falls through to `loadBuiltinLibrary()` instead — three genuinely
US-public-domain Project Gutenberg books (see the root README's Sources & inspirations section),
landing on the Library screen so there's something to look at immediately.

## Persistence

Independent `localStorage` keys, all scoped to the browser/origin they're used in (not shared with
real firmware, which would use small LittleFS records instead):

- `rotaryReaderLibrary_v1` — per-book resume position and bookmarks, keyed by `title::author`
  (not the book file itself — re-loading the *same* EPUB, or the built-in samples, restores where
  you left off; a different EPUB with the same title/author would collide, and a differently-titled
  copy of the same book wouldn't be recognized as the same book).
- `rotaryReaderReadingLog_v1` — per-book daily `{pages, seconds}` buckets backing
  [Reading stats](#reading-stats), also keyed by `title::author` and pruned to the most recent 120
  days per book.
- `rotaryReaderRulerScale_v1` — the true-size calibration slider's last value.
- `rotaryReaderTrueSizeDisplay_v1` — whether the "also resize the simulator's actual display"
  checkbox is on (see [True-size calibration](#true-size-calibration)).

All of them fail silently (try/catch) in private browsing or when storage is disabled — resume
position, bookmarks, reading stats, and calibration just won't persist in that case, rather than
the app erroring out.

## Deliberately not simulated: battery percentage

The stock CrowPanel schematic has no battery-voltage sense line at all — the MCU has zero
visibility into battery state as shipped, so there's nothing honest to display. The simulator
therefore shows no battery indicator anywhere (not on the sleep screen, not on boot). Firmware
mirrors this: `battery::readPercent()` returns `-1` while `PIN_BATTERY_ADC` is unset (the
default), and `main.cpp` only appends a percentage to the boot status line when the reading is
non-negative. Adding two SMD resistors as a divider and setting `PIN_BATTERY_ADC` in
`firmware/src/config.h` is what makes a real percentage available — see `battery.cpp`.

## Deliberately not simulated: portrait mode

The panel is a 122×250 sensor that's physically portrait-native, but that's roughly 19
characters per line — bad enough that portrait was never worth shipping as a real option. The
simulator renders unconditionally rotated (250×122 landscape), matching a hardcoded
`display.setRotation(1)` in `firmware/src/renderer.cpp`. There is no orientation toggle anywhere
in the UI; this is a deliberate simplification, not an oversight or an unfinished feature.

## In-app disclosure panel

The sidebar's "About this project" panel carries the same AI-assistance disclosure and the same
Sources & inspirations list as the root README, so anyone who lands on the deployed simulator
without going through GitHub still sees both. Keep the two in sync when either changes.

Below it, a separate "Project docs" panel pulls its content live from `docs/*.md` instead
(`loadDocsAbout`/`extractDocIntro`) — a title and first-paragraph summary per doc, fetched at
page load rather than retyped by hand, so it can't quietly drift from the actual files the way a
hand-copied summary could. This only works when `docs/` is actually reachable next to
`index.html`: [.github/workflows/pages.yml](../.github/workflows/pages.yml) publishes them as
siblings for exactly this reason. It fails gracefully (per-file, via `fetch`'s rejection) to a
plain "Read on GitHub" link wherever that layout doesn't hold — a bare
`cd simulator && python -m http.server` per the README's local-serve instructions, and always in
the published Claude Artifact, which is one self-contained file with nothing alongside it.

## What's simulator-only (nothing to port to firmware)

A few systems exist purely because a browser stands in for hardware that isn't in hand yet, and
have no real-firmware equivalent to write — see
[docs/FLASH_BUDGET.md](FLASH_BUDGET.md#what-doesnt-need-to-be-ported-at-all) for the full
reasoning:

- Ghosting/flash-sequence simulation (a real panel ghosts physically; firmware just calls
  GxEPD2's partial/full refresh).
- Fullscreen API, rotate prompt, haptics — stand-ins for not holding the physical device.
- `localStorage` for resume/bookmarks — a storage-backend choice (LittleFS/NVS) needs picking for
  firmware, not this exact behavior needing porting.
- Local-EPUB auto-load via directory listing — a dev-server convenience; firmware just lists
  whatever's already in `/books`.
