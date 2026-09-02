#pragma once
// Draws precomputed pages from a BookReader. Does no text layout — line breaks and
// page breaks were already decided by converter/converter.js (docs/FORMAT.md). This
// file only picks a font per style run and lets u8g2 advance the cursor.

#include "book_format.h"
#include <vector>

class Renderer {
 public:
  bool begin();

  void renderPage(BookReader& book, size_t chapterIdx, uint16_t pageIdx,
                   const std::vector<StyleRun>& chapterRuns);

  // Full-refresh single line of text, e.g. the boot-time "connect at 192.168.x.x" screen.
  void renderStatusLine(const String& text);

  void powerDown();  // display.hibernate() — call before deep/light sleep

 private:
  int pageTurnCounter_ = 0;
};
