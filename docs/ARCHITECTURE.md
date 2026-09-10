# Architecture

## Pipeline

1. **Browser converter** (`converter/`) unzips the EPUB with JSZip, walks `container.xml` → the
   OPF (manifest + spine) → each spine XHTML file with `DOMParser`. Everything except paragraph
   breaks, `h1`/`h2`, `b`/`strong`, `i`/`em` is discarded — no CSS, no images, no embedded fonts.
2. The converter line-wraps and paginates using **real glyph widths** dumped from the device
   (`tools/dump_font_metrics`), so the page breaks it computes are exactly what the firmware will
   render — the device does no text-layout work at runtime, just seeks and draws.
3. Output is a single `.cebk` file (`docs/FORMAT.md`). The converter can download it locally or
   `POST /upload` it straight to the device over the same connection it's being served from.
4. **Firmware** (`firmware/`) stores `.cebk` files in LittleFS (no SD slot on this board — 8MB
   flash total, shared with the firmware image, so realistic capacity is a handful of books; this
   is the main reason the format strips so aggressively instead of keeping raw HTML/markup).
5. Rendering seeks to the requested page's line range and draws each line, applying bold/italic/
   heading fonts where a style run overlaps — no measuring, no XML, no ZIP on-device.

## Layout constants are compile-time, not a user setting

Margin, line height, and font size (`firmware/src/config.h`, mirrored in
`converter/converter.js`'s `LAYOUT`) have to match on both sides of the pipeline, because page
breaks are computed once at conversion time and baked into each `.cebk` file's line/page tables —
that's the whole basis for the device doing zero text-layout work at runtime (see above). Changing
one of these constants after books are already converted silently desyncs their saved tables from
whatever the new constants would actually render.

`simulator/sim.html`'s Layout Parameters sliders (and the on-device Settings screen) look like a
live user preference — they re-paginate instantly — but that's a design-time convenience for
picking values before they're hardcoded into `config.h` and flashed, not a runtime capability real
firmware has. If per-user adjustable text size ever becomes a real feature, it needs on-device
re-flow (the firmware shipping a word-wrap routine after all, run once when a setting changes
rather than on every page turn) — a deliberate scope decision, not implemented here.

## Why preprocessing happens off-device instead of on the ESP32

The ESP32-S3 has enough RAM to parse EPUB (there are on-device EPUB readers, see below), but
doing it there means shipping a ZIP inflator + XML parser in firmware and re-running layout on
every page turn (or caching pagination results in flash, which is the same problem this format
solves anyway). Since a browser is already in the loop for uploading, and has orders of magnitude
more CPU/RAM than the target device, it's strictly cheaper to do the one-time expensive work there
and hand the device a format it can render with zero computation.

Precedent: [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader)
does the equivalent tag-stripping (keeps only `h1-h6`/`b`/`i`) on-device; Soldered's Inkplate
6FLICK reader uses a Python script on the host to pre-convert EPUBs before they reach the device.
This project follows the second pattern but in the browser instead of a desktop script, and pushes
pagination off-device too (neither precedent does that).

## Transport

Wi-Fi is opt-in. The radio is the expensive thing on this board; a held e-ink page is
nearly free. Default is `WIFI_OFF`, and nothing brings the radio up on its own — only an
explicit menu action does.

- Boot goes straight to the library. No associate, no portal, no connecting screen.
  Hold MENU at power-on as a secondary, menu-free way into the transfer AP (first boot,
  dead battery).
- Home → "Connect / transfer books" turns the AP on and jumps straight to its QR in one
  press — no need to separately open Wi-Fi and turn it on first. Home → Wi-Fi is the
  fuller screen: STA on (saved credentials)/off, the AP, and "Show QR code" once either
  is up.
- **The AP is the normal book-transfer path**, not a one-time setup fallback — reachable
  from the menu any time, long-lived once started (not tied to any portal UI's lifetime),
  and password-protected (`WIFI_AP_PASSWORD`, config.h) since the QR is what actually
  carries that to a phone, not a person typing it. Scanning it with a stock camera joins
  the network and a captive-portal prompt opens the uploader automatically (DNSServer
  answers every query with the AP's own IP; known OS probe paths — `/generate_204`,
  `/hotspot-detect.html`, etc. — get a 302 to `/` instead of the "you have internet" 204).
  No WiFiManager: it ran its own web server for as long as its portal was open, which
  can't share port 80 with the uploader — everything (uploader, captive-portal probes,
  the `/wifi/join` home-network form) is served by the one AsyncWebServer web_server.cpp
  owns. See wifi_setup.cpp's top comment for the fuller reasoning.
- Station mode also gets a QR (of `http://<ip>/`) once connected, so the desk workflow
  skips typing an IP too.
- The upload page is reachable whenever the shared server is up — AP or STA, same server.
- Sleep (idle-screen or EXIT hold) turns the radio off, but never fires while the AP is up
  — a light sleep or the sleep screen mid-upload would stall a transfer (see "Power"
  above). Wake does not turn the radio back on.
- Reconnect-with-backoff runs only while Wi-Fi was left on this session, and only in STA
  mode — the AP doesn't reconnect to anything.
- No RTC: stats before NTP (typical when offline) stay in the day-0 bucket.
- No `.local` mDNS; the IP is printed (and QR-coded) on the Wi-Fi screen when connected.

## Power

Idle is two stages, not one, because "the reader stopped touching it" and "the reader put it down"
are different events on very different timescales.

- **Light sleep**, after ~1.5s of quiet while reading (0.5s in menus). The panel keeps its image,
  the CPU halts, and any key wakes it — and the key that wakes it is *acted on*, so a page turn
  still costs exactly one press. This is invisible, which is what lets the gap be so short: the
  idle case between page turns is not a poll loop, it's a sleeping chip.
- **Sleep screen**, only after 10 minutes with no key at all. Wallpaper goes up, panel powers
  down, radio off. A light-sleep timer wakeup carries the deadline across the dozes above, so this
  fires on schedule without anything staying awake to count.

Light sleep is skipped while the radio is up — it stalls the web server mid-upload, and a device
you're uploading to isn't idle anyway.

Reading position is written to flash ~1s after the keys go quiet, not on a long timer. Pulling the
battery is how this device gets switched off, so that window is exactly how much progress an
unplug can cost. The book you were reading is saved by path alongside the position, so a cold boot
reopens *that* book rather than guessing from whichever title sorts first.

## Refresh strategy

Every frame is a full refresh (`renderer.cpp`'s `beginFrame()`) — no partial-refresh cadence.
There used to be one (an "every N pages" counter, the same convention most commercial e-readers
use), on the assumption that partial refresh's ghosting cost was worth trading for speed. On real
hardware that assumption didn't hold: the simulator models a ghosting cost for partial refresh
that the actual panel doesn't visibly pay, so there was no tradeoff left to make — see
[GitHub issue #12](https://github.com/trimpta/esp32-epaper-ereader/issues/12).

## Input

Rotary switch (`ROTARY_UP`/`ROTARY_DOWN`/`ROTARY_CONF`) maps directly to page turn / confirm with
no menu layer in between — that's the highest-frequency action and the only one worth optimizing
for zero indirection. MENU works the same way while reading: a tap turns the page (the same
zero-indirection reasoning as the wheel — it's the button right next to it), and holding it opens
the book's own menu (chapters/bookmarks/continue). Elsewhere (library, settings, games, ...) MENU
still just activates the focused row. EXIT is reserved for the general home menu and backing out
of screens, which can afford a slower redraw since they're comparatively rare.

## Explicitly out of scope for v1

- Images, embedded fonts, CSS, footnotes/links, nested lists, tables.
- True italic bitmap glyphs — u8g2's stock font set has very little italic/oblique coverage at
  small sizes. v1 ships without a real italic face; see `firmware/src/renderer.h` for the
  placeholder and how to swap in a converted font later if it matters enough to generate one.
- On-device library management beyond list/delete — no metadata sync, no covers, no search.
