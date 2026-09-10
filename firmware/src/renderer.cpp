#include "renderer.h"
#include "config.h"
#include "epd_gfx.h"

#include <algorithm>

static EpdGfx display;

static U8G2_FOR_ADAFRUIT_GFX u8f;

// Keep these font choices in sync with tools/dump_font_metrics/dump_font_metrics.ino —
// the browser converter paginates against widths dumped from these exact fonts.
static const uint8_t* fontForFlags(uint8_t flags) {
  if (flags & STYLE_H1) return u8g2_font_helvB12_tf;
  if (flags & STYLE_H2) return u8g2_font_helvB10_tf;
  if (flags & STYLE_BOLD) return u8g2_font_helvB08_tf;
  // No true italic face available at this size — falls back to regular.
  // See docs/ARCHITECTURE.md "Explicitly out of scope for v1".
  return u8g2_font_helvR08_tf;
}

// Splits [line.offset, line.offset+line.length) into sub-spans by style-run overlap, in
// render order. `runs` is sorted by offset, so `searchFrom` carries the scan position
// forward across spans and lines instead of re-scanning the whole chapter's run table for
// every span (which is what this did before — fine for a handful of runs, wasteful for a
// heavily-styled chapter with thousands).
static void forEachStyledSpan(const Line& line, const std::vector<StyleRun>& runs,
                               size_t& searchFrom,
                               const std::function<void(uint32_t, uint16_t, uint8_t)>& cb) {
  uint32_t pos = line.offset;
  uint32_t end = line.offset + line.length;
  while (searchFrom < runs.size() &&
         (uint32_t)(runs[searchFrom].offset + runs[searchFrom].length) <= pos) {
    searchFrom++;
  }
  while (pos < end) {
    uint8_t flags = 0;
    uint32_t spanEnd = end;
    for (size_t i = searchFrom; i < runs.size(); i++) {
      const StyleRun& r = runs[i];
      if (r.offset >= end) break;  // sorted: nothing further can overlap this line
      uint32_t rEnd = r.offset + r.length;
      if (pos >= r.offset && pos < rEnd) {
        flags |= r.flags;
        spanEnd = std::min(spanEnd, rEnd);
      } else if (r.offset > pos) {
        spanEnd = std::min(spanEnd, r.offset);
      }
    }
    cb(pos, (uint16_t)(spanEnd - pos), flags);
    pos = spanEnd;
  }
}

bool Renderer::begin() {
  display.begin();
  u8f.begin(display);
  u8f.setForegroundColor(INK);
  u8f.setBackgroundColor(PAPER);
  // Canvas is already 250x122 landscape (Elecrow horiz-2). Do not setRotation(1) — that
  // was for GxEPD2's 122x250 native buffer.

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(PAPER);
  } while (display.nextPage());
  return true;
}

// Always full refresh — see GitHub issue #12: the ghosting a partial refresh trades speed
// for is a real cost on this panel, but only in the simulator's model of one; on actual
// hardware there's no visible benefit to partial refresh worth the tradeoff, so every
// frame just clears fully. forceFullRefreshNext() is kept as a no-op call site rather than
// ripped out of every caller — it cost nothing to leave, and reads fine either way now.
void Renderer::beginFrame() {
  forceFull_ = false;
  display.setFullWindow();
}

void Renderer::renderPage(BookReader& book, size_t chapterIdx, uint16_t pageIdx,
                           const std::vector<StyleRun>& chapterRuns) {
  const auto lines = book.getPageLines(chapterIdx, pageIdx);

  // Pull the whole page's text in one seek+read. A page is ~10 lines of ~40 characters, so
  // this is well under 1KB; the previous version did a seek, a heap allocation and a String
  // construction per *styled span*, i.e. dozens of small reads per page turn.
  static uint8_t pageText[2048];
  uint32_t textBase = 0;
  size_t textLen = 0;
  if (!lines.empty()) {
    uint32_t minOff = lines.front().offset;
    uint32_t maxEnd = minOff;
    for (const auto& l : lines) {
      minOff = std::min(minOff, l.offset);
      maxEnd = std::max(maxEnd, (uint32_t)(l.offset + l.length));
    }
    if (maxEnd - minOff <= sizeof(pageText)) {
      textBase = minOff;
      textLen = book.readTextInto(minOff, (uint16_t)(maxEnd - minOff), pageText, sizeof(pageText));
    }
  }

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(PAPER);
    u8f.setForegroundColor(INK);
    u8f.setBackgroundColor(PAPER);
    int y = MARGIN_PX + LINE_HEIGHT_PX;  // baseline of first line
    size_t runCursor = 0;
    for (const auto& line : lines) {
      int x = MARGIN_PX;
      bool heading = false;
      forEachStyledSpan(line, chapterRuns, runCursor, [&](uint32_t off, uint16_t len, uint8_t flags) {
        if (flags & (STYLE_H1 | STYLE_H2)) heading = true;
        u8f.setFont(fontForFlags(flags));
        u8f.setCursor(x, y);
        if (textLen > 0 && off >= textBase && (off - textBase) + len <= textLen) {
          // Byte-at-a-time because U8G2_FOR_ADAFRUIT_GFX declares write(uint8_t) without a
          // `using Print::write;`, which hides Print's buffer overload. Still allocation-free
          // and still one file read per page, which is the point.
          const uint8_t* p = pageText + (off - textBase);
          for (uint16_t i = 0; i < len; i++) u8f.write(p[i]);
        } else {
          u8f.print(book.readText(off, len));  // page didn't fit the buffer — fall back
        }
        x = u8f.getCursorX();
      });
      y += heading ? HEADING_LINE_HEIGHT_PX : LINE_HEIGHT_PX;
    }
  } while (display.nextPage());
}

void Renderer::renderList(const std::vector<ListRow>& rows, int focusedSelectableIdx) {
  // Scroll window: find the row index of the focused selectable row, then take a window of
  // rows around it that fits the panel. Separators count as half a line, same as the
  // simulator, so a library list of title+author blocks lands identically.
  const int maxUnits = (PAGE_HEIGHT_PX - LIST_TOP_MARGIN_PX - 4) / LIST_LINE_HEIGHT_PX;
  int focusRow = 0, sel = 0;
  for (size_t i = 0; i < rows.size(); i++) {
    if (!rows[i].selectable) continue;
    if (sel == focusedSelectableIdx) {
      focusRow = (int)i;
      break;
    }
    sel++;
  }
  int start = 0;
  if ((int)rows.size() > maxUnits) {
    start = focusRow - maxUnits / 2;
    start = std::max(0, std::min(start, (int)rows.size() - maxUnits));
  }

  beginFrame();
  display.firstPage();
  do {
    display.fillScreen(PAPER);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(INK);
    // helvR08 is proportional: ">" is wider than a space, so prefixing "> " used to shove
    // the whole label right and force a full-row DU. Keep labels at a fixed x; only the
    // caret column toggles when focus moves.
    const int caretW = (int)u8f.getUTF8Width(">");
    const int gap = (int)u8f.getUTF8Width(" ");
    const int labelX = MARGIN_PX + caretW + gap;
    int y = LIST_FIRST_BASELINE_PX;
    int drawn = 0;
    for (size_t i = (size_t)start; i < rows.size() && drawn < maxUnits; i++, drawn++) {
      const ListRow& row = rows[i];
      if (row.separator) {
        display.drawFastHLine(MARGIN_PX, y - 5, PAGE_WIDTH_PX - 2 * MARGIN_PX, INK);
        y += LIST_LINE_HEIGHT_PX / 2;
        continue;
      }
      if ((int)i == focusRow) {
        u8f.setCursor(MARGIN_PX, y);
        u8f.print(">");
      }
      // Dim meta lines (author, hints) get one extra space of indent, constant per row
      // so it never fights the caret.
      u8f.setCursor(labelX + (row.dim ? gap : 0), y);
      u8f.print(row.text);
      y += LIST_LINE_HEIGHT_PX;
    }
  } while (display.nextPage());
}

void Renderer::renderMessage(const String* lines, size_t lineCount) {
  display.setFullWindow();
  forceFull_ = false;
  display.firstPage();
  do {
    display.fillScreen(PAPER);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(INK);
    int y = MARGIN_PX + LINE_HEIGHT_PX;
    for (size_t i = 0; i < lineCount; i++) {
      u8f.setCursor(MARGIN_PX, y);
      u8f.print(lines[i]);
      y += LINE_HEIGHT_PX;
    }
  } while (display.nextPage());
}

void Renderer::renderStatusLine(const String& text) {
  renderMessage(&text, 1);
}

void Renderer::renderSleep(const String& title, const String& extra, const uint8_t* wallpaperBits) {
  display.setFullWindow();
  forceFull_ = false;
  display.firstPage();
  do {
    display.fillScreen(PAPER);
    if (wallpaperBits) {
      display.copyBuffer(wallpaperBits);
    }
    if (!title.length() && !extra.length()) continue;
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(INK);
    u8f.setBackgroundColor(PAPER);
    const int pad = 4;
    const int maxW = PAGE_WIDTH_PX - 2 * pad;
    const char* t = title.c_str();
    const char* e = extra.c_str();
    int titleW = title.length() ? (int)u8f.getUTF8Width(t) : 0;
    int extraW = extra.length() ? (int)u8f.getUTF8Width(e) : 0;
    int gap = (titleW && extraW) ? (int)u8f.getUTF8Width("  ") : 0;
    bool twoLines = extraW && titleW && (titleW + gap + extraW > maxW);
    if (!twoLines) {
      int w = titleW + gap + extraW;
      display.fillRect(2, PAGE_HEIGHT_PX - 14, w + 4, 12, PAPER);
      u8f.setCursor(pad, PAGE_HEIGHT_PX - 5);
      if (title.length()) u8f.print(t);
      if (extra.length()) {
        if (title.length()) u8f.print("  ");
        u8f.print(e);
      }
    } else {
      display.fillRect(2, PAGE_HEIGHT_PX - 26, PAGE_WIDTH_PX - 4, 24, PAPER);
      u8f.setCursor(pad, PAGE_HEIGHT_PX - 16);
      u8f.print(t);
      u8f.setCursor(pad, PAGE_HEIGHT_PX - 5);
      u8f.print(e);
    }
  } while (display.nextPage());
}

void Renderer::renderCustom(const std::function<void(Adafruit_GFX&, U8G2_FOR_ADAFRUIT_GFX&)>& draw) {
  beginFrame();
  display.firstPage();
  do {
    display.fillScreen(PAPER);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(INK);
    draw(display, u8f);
  } while (display.nextPage());
}

void Renderer::powerDown() {
  display.hibernate();
}

void Renderer::wakeDisplay() {
  display.wake();
  forceFull_ = true;
}
