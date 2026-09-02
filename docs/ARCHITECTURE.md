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

`simulator/index.html`'s Layout Parameters sliders (and the on-device Settings screen) look like a
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

- **Persistent STA WiFi + mDNS-less discovery.** The device joins a saved network (phone hotspot
  or home WiFi — same code path either way) instead of running its own AP that you'd have to
  switch to per upload. `.local` mDNS resolution is unreliable from Android browsers even on your
  own hotspot, so the device prints its assigned IP directly on the e-paper screen after
  connecting instead of relying on `ereader.local`.
- First-time (or re-)provisioning uses a captive portal (WiFiManager), entered only when there are
  no saved credentials or the MENU button is held at boot — not part of the normal flow.
- Reconnect uses retry-with-backoff rather than re-provisioning, since phone hotspots commonly
  sleep after inactivity and come back with the same SSID/password.

## Refresh strategy

E-ink partial refresh (~300ms) is used for every page turn; ghosting is cleared with a full
refresh every ~8–10 pages (same cadence convention as most commercial e-readers), tracked with a
simple counter rather than a timer, so it's tied to actual reading activity.

## Input

Rotary switch (`ROTARY_UP`/`ROTARY_DOWN`/`ROTARY_CONF`) maps directly to page turn / confirm with
no menu layer in between — that's the highest-frequency action and the only one worth optimizing
for zero indirection. MENU/EXIT are reserved for book list, TOC, and settings screens, which can
afford a slower full-refresh redraw since they're rare.

## Explicitly out of scope for v1

- Images, embedded fonts, CSS, footnotes/links, nested lists, tables.
- True italic bitmap glyphs — u8g2's stock font set has very little italic/oblique coverage at
  small sizes. v1 ships without a real italic face; see `firmware/src/renderer.h` for the
  placeholder and how to swap in a converted font later if it matters enough to generate one.
- On-device library management beyond list/delete — no metadata sync, no covers, no search.
