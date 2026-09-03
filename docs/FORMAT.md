# .cebk binary format (v1)

"Custom E-Book". Everything is little-endian. Produced by `converter/converter.js`, consumed by
`firmware/src/book_format.h`. Both sides must agree on this file — bump the version byte on any
layout change.

Design goal: the device does **zero text layout**. Line-wrapping and page-breaking happen once,
in the browser, against real glyph-width data pulled from the device (see
`tools/dump_font_metrics`). The device just seeks to an offset and draws precomputed lines.

## Top-level layout

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | Magic: ASCII `"CEBK"` |
| 4 | 1 | Format version (`1`) |
| 5 | 1 | `titleLen` (Lt) |
| 6 | Lt | `title`, UTF-8 |
| 6+Lt | 1 | `authorLen` (La) |
| ... | La | `author`, UTF-8 |
| ... | 2 | `chapterCount` (uint16) |
| ... | — | `chapterCount` × **chapter table entries** (variable length, see below) |
| ... | 4 | `textBlobLength` (uint32) |
| ... | textBlobLength | `textBlob` — UTF-8 text of every chapter, concatenated in order |

All offsets referenced from chapter/line/style-run entries below are **absolute byte offsets into
`textBlob`**, not relative to the chapter start — simpler to reason about on both sides, and the
cost (a few extra bytes per uint32) doesn't matter at this scale.

## Chapter table entry

| Size | Field |
|---|---|
| 1 | `titleLen` (Lc) |
| Lc | chapter `title`, UTF-8 |
| 4 | `textOffset` (uint32) — start of this chapter's text in `textBlob` |
| 4 | `textLength` (uint32) |
| 2 | `lineCount` (uint16) |
| lineCount × 6 | **line entries**: `{ offset: uint32, length: uint16 }` — one precomputed, already-wrapped line |
| 2 | `pageCount` (uint16) |
| pageCount × 2 | **page starts**: `uint16` index into this chapter's line array where each page begins |
| 2 | `styleRunCount` (uint16) |
| styleRunCount × 7 | **style runs**: `{ offset: uint32, length: uint16, flags: uint8 }` |

`flags` bitfield: `bit0` = bold, `bit1` = italic, `bit2` = heading-1, `bit3` = heading-2. Headings
imply bold at a larger font on-device; the converter never mixes heading with inline bold/italic
runs (a heading's whole line is one run).

## Rendering a page (device side, no measuring)

```
page = chapter.pageStarts[pageIndex]
nextPage = chapter.pageStarts[pageIndex + 1] or chapter.lineCount
cursor.y = TOP_MARGIN
for lineIdx in [page, nextPage):
    line = chapter.lines[lineIdx]
    cursor.x = LEFT_MARGIN
    for (spanOffset, spanLength, styleFlags) in splitByOverlappingStyleRuns(line, chapter.styleRuns):
        setFont(fontForStyle(styleFlags))       # regular/bold/italic/bold-italic/h1/h2
        u8g2.setCursor(cursor.x, cursor.y)
        u8g2.print(textBlob[spanOffset : spanOffset+spanLength])
        cursor.x = u8g2.getCursorX()             # u8g2 advances the cursor itself
    cursor.y += LINE_HEIGHT_PX
```

`splitByOverlappingStyleRuns` is a linear scan (style runs per line are almost always 0–2) — no
width computation on-device, u8g2's own cursor advance handles that.

## Layout constants the converter must paginate against

Defined once in `converter/converter.js` (`LAYOUT` object) and mirrored in
`firmware/src/config.h`. Keep them in sync manually — v1 doesn't embed them in the file.

- `PAGE_WIDTH_PX = 250`, `PAGE_HEIGHT_PX = 122` — landscape. The panel is natively
  122×250 portrait, but that's ~19 characters/line; firmware renders rotated
  (`display.setRotation(1)`, a draw-surface transpose, not a hardware change) for a
  reading-width line length. See `firmware/src/config.h` and `simulator/sim.html`.
- `MARGIN_PX = 4` on all sides
- `LINE_HEIGHT_PX = 11` (regular/bold/italic body text)
- `HEADING_LINE_HEIGHT_PX = 14`

## Why not store per-line style flags instead of a separate run table?

A run table is smaller (most chapters are almost entirely regular text — runs are the exception,
not the rule) and keeps the line table trivial to generate: the line-wrapper doesn't need to know
or care about styling, it just wraps a flat text+width stream. Styling is overlaid afterward by
intersecting line spans with the independently-tracked run list.
