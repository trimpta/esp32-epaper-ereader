#include <Arduino.h>
#include <LittleFS.h>

#include "battery.h"
#include "book_format.h"
#include "config.h"
#include "input.h"
#include "renderer.h"
#include "web_server.h"
#include "wifi_setup.h"

namespace {
Renderer renderer;
BookReader currentBook;
std::vector<StyleRun> currentChapterRuns;
size_t currentChapterIdx = 0;
uint16_t currentPageIdx = 0;

// Auto-loads whatever's first in /books. No book-picker UI in this skeleton — MENU/EXIT
// are wired up (input.h) but a library list / TOC screen isn't implemented yet.
void openFirstBook() {
  File dir = LittleFS.open(BOOKS_DIR);
  if (!dir) return;
  File f = dir.openNextFile();
  if (!f) return;
  String path = String(BOOKS_DIR) + "/" + f.name();
  if (currentBook.open(LittleFS, path.c_str())) {
    currentChapterIdx = 0;
    currentPageIdx = 0;
    currentChapterRuns = currentBook.getChapterRuns(0);
  }
}

void showCurrentPage() {
  if (!currentBook.isOpen()) return;
  renderer.renderPage(currentBook, currentChapterIdx, currentPageIdx, currentChapterRuns);
}

void goNextPage() {
  if (!currentBook.isOpen()) return;
  const ChapterIndex& ch = currentBook.chapter(currentChapterIdx);
  if (currentPageIdx + 1 < ch.pageCount) {
    currentPageIdx++;
  } else if (currentChapterIdx + 1 < currentBook.chapterCount()) {
    currentChapterIdx++;
    currentPageIdx = 0;
    currentChapterRuns = currentBook.getChapterRuns(currentChapterIdx);
  } else {
    return;  // already at the last page of the last chapter
  }
  showCurrentPage();
}

void goPrevPage() {
  if (!currentBook.isOpen()) return;
  if (currentPageIdx > 0) {
    currentPageIdx--;
  } else if (currentChapterIdx > 0) {
    currentChapterIdx--;
    currentChapterRuns = currentBook.getChapterRuns(currentChapterIdx);
    currentPageIdx = currentBook.chapter(currentChapterIdx).pageCount - 1;
  } else {
    return;  // already at the first page
  }
  showCurrentPage();
}
}  // namespace

void setup() {
  Serial.begin(115200);
  input::begin();
  LittleFS.begin(true);
  renderer.begin();

  // Hold MENU while powering on to re-enter WiFi setup.
  bool forcePortal = (digitalRead(PIN_MENU_BUTTON) == LOW);

  renderer.renderStatusLine("Connecting to WiFi...");
  wifi_setup::begin(forcePortal, [](const String& ip) {
    // Printed directly rather than relying on ereader.local — see wifi_setup.h.
    String status = "Connect at: " + ip;
    int pct = battery::readPercent();
    // -1 until PIN_BATTERY_ADC in config.h is wired up — see battery.h.
    if (pct >= 0) status += "   Batt: " + String(pct) + "%";
    renderer.renderStatusLine(status);
    delay(2000);
  });

  web_server::begin([]() {
    // TODO: a book finished uploading. This skeleton doesn't have a library/picker
    // screen yet, so newly uploaded books just wait in /books until openFirstBook()
    // runs again (i.e. next boot). Wire this callback up to a book-list screen once
    // one exists.
  });

  openFirstBook();
  showCurrentPage();
}

void loop() {
  wifi_setup::poll();

  switch (input::poll()) {
    case InputEvent::PageNext:
      goNextPage();
      break;
    case InputEvent::PagePrev:
      goPrevPage();
      break;
    case InputEvent::Menu:
    case InputEvent::Exit:
    case InputEvent::Confirm:
      // Reserved for book list / TOC / settings screens — not implemented in this
      // skeleton.
      break;
    case InputEvent::None:
    default:
      break;
  }

  // Simple poll-and-delay loop for now. Once the ext1-wakeup pin set is confirmed on
  // real hardware (see input.h TODO), swap this for input::enterLightSleepUntilInput()
  // to actually save power between button presses.
  delay(20);
}
