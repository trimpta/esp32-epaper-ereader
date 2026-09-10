#pragma once
// Draws precomputed pages from a BookReader, plus the menu/list, sleep and game screens.
// Does no text layout for book pages — line breaks and page breaks were already decided by
// converter/converter.js (docs/FORMAT.md). This file only picks a font per style run and
// lets u8g2 advance the cursor.
//
// Everything that touches the e-paper driver lives behind this class: games and menus
// draw through renderCustom()/renderList() rather than reaching for the display themselves.

#include "book_format.h"

#include <Adafruit_GFX.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <functional>
#include <vector>

// 1-bit panel: two colors, named here so callers don't need the panel driver headers.
static const uint16_t INK = 0x0000;
static const uint16_t PAPER = 0xFFFF;

// One line of a menu screen. A "block" (e.g. a library entry: title + dimmed author line)
// is just consecutive rows where only the first is selectable — mirroring the simulator's
// screenBlocks()/selectableBlocks() split.
struct ListRow {
  String text;
  bool dim = false;
  bool separator = false;
  bool selectable = true;

  ListRow() {}
  ListRow(const String& t, bool selectable_ = true, bool dim_ = false)
      : text(t), dim(dim_), selectable(selectable_) {}
  static ListRow sep() {
    ListRow r;
    r.separator = true;
    r.selectable = false;
    return r;
  }
};

class Renderer {
 public:
  bool begin();

  void renderPage(BookReader& book, size_t chapterIdx, uint16_t pageIdx,
                   const std::vector<StyleRun>& chapterRuns);

  // Scrollable menu list. `focusedSelectableIdx` counts only selectable rows, and the
  // visible window follows it — same windowing behavior as the simulator's fitWindow().
  void renderList(const std::vector<ListRow>& rows, int focusedSelectableIdx);

  // Full-refresh single line of text, e.g. the boot-time "connect at 192.168.x.x" screen.
  void renderStatusLine(const String& text);
  void renderMessage(const String* lines, size_t lineCount);

  // Sleep screen: wallpaper if loaded (raw native 4000-byte panel buffer, 1=paper 0=ink),
  // otherwise blank paper, with title (and optional second line) along the bottom.
  void renderSleep(const String& title, const String& extra, const uint8_t* wallpaperBits);

  // Arbitrary drawing (the games). The callback runs inside the paged-draw loop.
  void renderCustom(const std::function<void(Adafruit_GFX&, U8G2_FOR_ADAFRUIT_GFX&)>& draw);

  // No-op now that beginFrame() always does a full refresh (see renderer.cpp / GitHub
  // issue #12) — kept so existing call sites (screen transitions, wake) don't need
  // touching; they still read fine as "make sure this one's a full redraw".
  void forceFullRefreshNext() { forceFull_ = true; }

  void powerDown();  // display.hibernate() — call before sleeping
  void wakeDisplay();  // re-init the panel after hibernate so the next frame can draw

 private:
  void beginFrame();  // always full — see forceFullRefreshNext()
  bool forceFull_ = false;
};
