#include "renderer.h"
#include "config.h"

#include <GxEPD2_BW.h>
#include <SPI.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <algorithm>
#include <functional>

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
// render order. Runs per line are almost always 0-2, so a linear scan is fine — no need
// for anything cleverer at this scale.
static void forEachStyledSpan(const Line& line, const std::vector<StyleRun>& runs,
                               const std::function<void(uint32_t, uint16_t, uint8_t)>& cb) {
  uint32_t pos = line.offset;
  uint32_t end = line.offset + line.length;
  while (pos < end) {
    uint8_t flags = 0;
    uint32_t spanEnd = end;
    for (const auto& r : runs) {
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
  // before GxEPD2 touches it.
  SPI.begin(PIN_EPD_SCK, -1 /* MISO unused */, PIN_EPD_MOSI, PIN_EPD_CS);
  display.init(115200, true, 20, false, SPI);
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

void Renderer::renderPage(BookReader& book, size_t chapterIdx, uint16_t pageIdx,
                           const std::vector<StyleRun>& chapterRuns) {
  const auto lines = book.getPageLines(chapterIdx, pageIdx);

  // Full refresh every N page turns to clear partial-refresh ghosting; see
  // docs/ARCHITECTURE.md "Refresh strategy".
  bool partial = (pageTurnCounter_ % FULL_REFRESH_EVERY_N_PAGES) != 0;
  if (partial) {
    display.setPartialWindow(0, 0, PAGE_WIDTH_PX, PAGE_HEIGHT_PX);
  } else {
    display.setFullWindow();
  }

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    int y = MARGIN_PX + LINE_HEIGHT_PX;  // baseline of first line
    for (const auto& line : lines) {
      int x = MARGIN_PX;
      bool heading = false;
      forEachStyledSpan(line, chapterRuns, [&](uint32_t off, uint16_t len, uint8_t flags) {
        if (flags & (STYLE_H1 | STYLE_H2)) heading = true;
        u8f.setFont(fontForFlags(flags));
        u8f.setCursor(x, y);
        u8f.print(book.readText(off, len));
        x = u8f.getCursorX();
      });
      y += heading ? HEADING_LINE_HEIGHT_PX : LINE_HEIGHT_PX;
    }
  } while (display.nextPage());

  pageTurnCounter_++;
}

void Renderer::renderStatusLine(const String& text) {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    u8f.setFont(u8g2_font_helvR08_tf);
    u8f.setCursor(MARGIN_PX, PAGE_HEIGHT_PX / 2);
    u8f.print(text);
  } while (display.nextPage());
}

void Renderer::powerDown() {
  display.hibernate();
}
