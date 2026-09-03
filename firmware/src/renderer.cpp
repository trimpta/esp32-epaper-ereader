#include "renderer.h"
#include "config.h"
#include "settings.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <algorithm>

// TODO(verify): GxEPD2_213_BN is GxEPD2's generic 122x250 SSD1680 2.13" panel definition.
// Confirm this matches the actual JD79661/SSD1680Z panel timing on this board — if GxEPD2
// doesn't support JD79661 at all, this whole file needs to move to Elecrow's own EPD
// library instead (see README.md "Status / what needs real-hardware verification").
static GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT> display(
    GxEPD2_213_BN(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

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
  // Panel's 3.3V rail is gated by this pin (schematic: IO7_LCD_3.3_CTL) — nothing on the
  // SPI lines responds until it's driven high. Give the rail a moment to settle.
  pinMode(PIN_EPD_POWER_CTL, OUTPUT);
  digitalWrite(PIN_EPD_POWER_CTL, HIGH);
  delay(10);

  // MOSI/SCK (IO11/IO12) aren't ESP32-S3's default VSPI pins, so SPI needs explicit begin()
  // before GxEPD2 touches it. GxEPD2 has no init() overload taking a bare SPIClass& — the
  // bus is selected separately, via the panel object.
  SPI.begin(PIN_EPD_SCK, -1 /* MISO unused */, PIN_EPD_MOSI, PIN_EPD_CS);
  display.epd2.selectSPI(SPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 20, false);
  u8f.begin(display);
  // Landscape — see config.h PAGE_WIDTH_PX/PAGE_HEIGHT_PX comment for why.
  display.setRotation(1);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
  } while (display.nextPage());
  return true;
}

// Full refresh on the configured cadence (or on demand), partial in between; see
// docs/ARCHITECTURE.md "Refresh strategy". The counter is shared by every screen, not just
// book pages, so ghosting accumulated in a menu is cleared on the same schedule.
void Renderer::beginFrame() {
  bool full = forceFull_ || (pageTurnCounter_ % settings::refreshEveryNPages()) == 0;
  forceFull_ = false;
  pageTurnCounter_++;
  if (full) {
    display.setFullWindow();
  } else {
    display.setPartialWindow(0, 0, PAGE_WIDTH_PX, PAGE_HEIGHT_PX);
  }
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

  beginFrame();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
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
    display.fillScreen(GxEPD_WHITE);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(GxEPD_BLACK);
    int y = LIST_FIRST_BASELINE_PX;
    int drawn = 0;
    for (size_t i = (size_t)start; i < rows.size() && drawn < maxUnits; i++, drawn++) {
      const ListRow& row = rows[i];
      if (row.separator) {
        display.drawFastHLine(MARGIN_PX, y - 5, PAGE_WIDTH_PX - 2 * MARGIN_PX, GxEPD_BLACK);
        y += LIST_LINE_HEIGHT_PX / 2;
        continue;
      }
      // A 1-bit panel has no "dimmed" — the simulator's 60%-alpha meta lines become an
      // indent here instead, which reads as secondary without needing a gray it can't show.
      const char* prefix = ((int)i == focusRow) ? "> " : (row.dim ? "   " : "  ");
      u8f.setCursor(MARGIN_PX, y);
      u8f.print(prefix);
      u8f.print(row.text);
      y += LIST_LINE_HEIGHT_PX;
    }
  } while (display.nextPage());
}

void Renderer::renderMessage(const String* lines, size_t lineCount) {
  display.setFullWindow();
  forceFull_ = false;
  pageTurnCounter_ = 1;  // next frame is a partial; this one already cleared the panel
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(GxEPD_BLACK);
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

void Renderer::renderSleep(const String& bottomText, const uint8_t* wallpaperBits) {
  display.setFullWindow();
  forceFull_ = false;
  pageTurnCounter_ = 1;
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (wallpaperBits) {
      // Panel-native portrait bitmap (122x250, 1bpp). setRotation(1) is still in effect, so
      // GFX transposes it onto the landscape surface for us.
      display.drawBitmap(0, 0, wallpaperBits, 122, 250, GxEPD_BLACK);
    }
    if (bottomText.length()) {
      u8f.setFont(u8g2_font_helvR08_tf);
      u8f.setForegroundColor(GxEPD_BLACK);
      u8f.setBackgroundColor(GxEPD_WHITE);
      // Solid backing plate so a busy wallpaper never runs through the glyphs — same
      // legibility-over-aesthetics call the simulator's sleep screen makes.
      int textW = (int)u8f.getUTF8Width(bottomText.c_str());
      display.fillRect(2, PAGE_HEIGHT_PX - 14, textW + 4, 12, GxEPD_WHITE);
      u8f.setCursor(4, PAGE_HEIGHT_PX - 5);
      u8f.print(bottomText);
    }
  } while (display.nextPage());
}

void Renderer::renderCustom(const std::function<void(Adafruit_GFX&, U8G2_FOR_ADAFRUIT_GFX&)>& draw) {
  beginFrame();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setForegroundColor(GxEPD_BLACK);
    draw(display, u8f);
  } while (display.nextPage());
}

void Renderer::powerDown() {
  display.hibernate();
}
