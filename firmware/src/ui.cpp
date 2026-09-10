#include "ui.h"

#include "book_format.h"
#include "config.h"
#include "games.h"
#include "library.h"
#include "qr_screen.h"
#include "renderer.h"
#include "settings.h"
#include "wifi_setup.h"

#include <LittleFS.h>
#include <algorithm>
#include <vector>

namespace {

enum class Screen {
  Reading,
  Home,
  Library,
  BookMenu,
  BookChapters,
  Bookmarks,
  Stats,
  AllStats,
  ConfirmDelete,
  Settings,
  WiFi,
  WifiQr,  // full-screen QR (AP-join or STA URL) pushed on top of WiFi
  Games,
  GameScreen,
};

Renderer* rend = nullptr;

// Navigation is a stack, not a single mode — EXIT pops, a row's action pushes. Mirrors
// state.screenStack in the simulator, including the special case that popping past the
// bottom lands back in the book you were reading.
std::vector<Screen> stack;
int focus = 0;

int activeBook = -1;   // the book currently open for reading
int browseBook = -1;   // the book a book-context screen (BookMenu/Chapters/Bookmarks/Stats) is about
BookReader currentBook;
std::vector<StyleRun> currentRuns;
size_t currentRunsChapter = (size_t)-1;
uint16_t chapterIdx = 0, pageIdx = 0;

bool asleep = false;
bool editingRefresh = false;  // rotary-hold scrub session on the refresh-cadence setting
int editOriginal = 0;
bool lastWifiConnected = false;
bool lastApOpen = false;

unsigned long lastInputMs = 0;
unsigned long lastReadingTickMs = 0;
unsigned long lastFlushMs = 0;
const unsigned long READING_TICK_MS = 15000;  // stats granularity; also bounds flash writes
// How long the state file may lag behind the screen. Pulling the battery is how this
// device gets switched off, so this is the window in which a page turn can be lost — the
// old 30s meant the last few pages of a session routinely were. Short enough to survive an
// unplug, long enough that holding the wheel down is still one write, not one per page.
const unsigned long FLUSH_DEBOUNCE_MS = 1000;

std::vector<String> wallpaperFiles;
uint8_t wallpaperBuf[WALLPAPER_BYTES];

Screen current() {
  return stack.empty() ? Screen::Reading : stack.back();
}

void scanWallpapers() {
  wallpaperFiles.clear();
  File dir = LittleFS.open(WALLPAPER_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    f.close();
    // Dirent size is 0 on some ESP32 LittleFS builds — open the file to measure it.
    File body = LittleFS.open(String(WALLPAPER_DIR) + "/" + name, "r");
    size_t sz = body ? body.size() : 0;
    if (body) body.close();
    if (sz == WALLPAPER_BYTES) wallpaperFiles.push_back(name);
  }
  dir.close();
  std::sort(wallpaperFiles.begin(), wallpaperFiles.end());
}

// Returns nullptr when there's no wallpaper selected or it can't be read — the sleep
// screen then just shows the book title on blank paper.
const uint8_t* loadSelectedWallpaper() {
  int idx = settings::wallpaperIndex();
  if (idx < 0 || idx >= (int)wallpaperFiles.size()) return nullptr;
  File f = LittleFS.open(String(WALLPAPER_DIR) + "/" + wallpaperFiles[idx], "r");
  if (!f) return nullptr;
  size_t got = f.read(wallpaperBuf, sizeof(wallpaperBuf));
  f.close();
  return got == sizeof(wallpaperBuf) ? wallpaperBuf : nullptr;
}

String bookLabel(const BookMeta& b) {
  return b.title.length() ? b.title : String("(untitled)");
}

String fmtDuration(uint32_t seconds) {
  uint32_t minutes = (seconds + 30) / 60;
  if (minutes < 60) return String(minutes) + "m";
  return String(minutes / 60) + "h " + String(minutes % 60) + "m";
}

// ---------------------------------------------------------------- book open / paging

void loadChapterRuns() {
  if (currentRunsChapter == chapterIdx) return;
  currentRuns = currentBook.getChapterRuns(chapterIdx);
  currentRunsChapter = chapterIdx;
}

void renderReading() {
  if (!currentBook.isOpen()) {
    String msg = "No book open";
    rend->renderStatusLine(msg);
    return;
  }
  loadChapterRuns();
  rend->renderPage(currentBook, chapterIdx, pageIdx, currentRuns);
}

void renderCurrent();

bool openBook(int idx, uint16_t chapter, uint16_t page) {
  if (idx < 0 || idx >= (int)library::count()) return false;
  BookMeta& meta = library::book(idx);
  currentBook.close();
  currentRuns.clear();
  currentRunsChapter = (size_t)-1;
  if (!currentBook.open(LittleFS, meta.path.c_str())) {
    activeBook = -1;
    return false;
  }
  activeBook = idx;
  library::setActiveBook(idx);
  chapterIdx = (uint16_t)std::min<size_t>(chapter, currentBook.chapterCount() ? currentBook.chapterCount() - 1 : 0);
  uint16_t maxPage = currentBook.chapterCount() ? currentBook.chapter(chapterIdx).pageCount : 0;
  pageIdx = maxPage ? (uint16_t)std::min<uint16_t>(page, maxPage - 1) : 0;
  library::setPosition(idx, chapterIdx, pageIdx);
  return true;
}

void goToReading() {
  stack.clear();
  stack.push_back(Screen::Reading);
  focus = 0;
  if (rend) rend->forceFullRefreshNext();
}

void nextPage() {
  if (!currentBook.isOpen() || activeBook < 0) return;
  const ChapterIndex& ch = currentBook.chapter(chapterIdx);
  if (pageIdx + 1 < ch.pageCount) {
    pageIdx++;
  } else if (chapterIdx + 1 < currentBook.chapterCount()) {
    chapterIdx++;
    pageIdx = 0;
  } else {
    return;
  }
  library::setPosition(activeBook, chapterIdx, pageIdx);
  library::logPageTurn(activeBook);
  renderReading();
}

bool skipReading(int dir) {
  if (!currentBook.isOpen() || activeBook < 0) return false;
  if (dir > 0) {
    const ChapterIndex& ch = currentBook.chapter(chapterIdx);
    if (pageIdx + 1 < ch.pageCount) {
      pageIdx++;
    } else if (chapterIdx + 1 < currentBook.chapterCount()) {
      chapterIdx++;
      pageIdx = 0;
    } else {
      return false;
    }
    library::setPosition(activeBook, chapterIdx, pageIdx);
    library::logPageTurn(activeBook);
    return true;
  }
  if (pageIdx > 0) {
    pageIdx--;
  } else if (chapterIdx > 0) {
    chapterIdx--;
    uint16_t pc = currentBook.chapter(chapterIdx).pageCount;
    pageIdx = pc ? (uint16_t)(pc - 1) : 0;
  } else {
    return false;
  }
  library::setPosition(activeBook, chapterIdx, pageIdx);
  return true;
}

void prevPage() {
  if (!currentBook.isOpen() || activeBook < 0) return;
  if (pageIdx > 0) {
    pageIdx--;
  } else if (chapterIdx > 0) {
    chapterIdx--;
    uint16_t pc = currentBook.chapter(chapterIdx).pageCount;
    pageIdx = pc ? (uint16_t)(pc - 1) : 0;
  } else {
    return;
  }
  library::setPosition(activeBook, chapterIdx, pageIdx);
  renderReading();
}

// ---------------------------------------------------------------- row building
// One builder per screen, matching the simulator's *Rows() functions. Rows are built fresh
// on every render (they're a handful of Strings — cheaper than keeping them in sync).

struct Row {
  ListRow row;
  int action;  // index the dispatcher switches on; -1 = inert display line
  int param = 0;
};

std::vector<Row> buildRows();

void pushScreen(Screen s) {
  stack.push_back(s);
  focus = 0;
  if (rend) rend->forceFullRefreshNext();
}

void popScreen() {
  if (stack.size() > 1) {
    stack.pop_back();
    focus = 0;
    if (rend) rend->forceFullRefreshNext();
  } else if (activeBook >= 0) {
    goToReading();
  }
}

enum {
  ACT_NONE = -1,
  ACT_CONTINUE_ACTIVE = 1,
  ACT_BOOKMARK_HERE,
  ACT_OPEN_BOOKMARKS,
  ACT_OPEN_LIBRARY,
  ACT_OPEN_GAMES,
  ACT_OPEN_SETTINGS,
  ACT_OPEN_WIFI,
  ACT_PICK_BOOK,
  ACT_CONTINUE_BOOK,
  ACT_START_BOOK,
  ACT_OPEN_CHAPTERS,
  ACT_OPEN_STATS,
  ACT_OPEN_ALL_STATS,
  ACT_OPEN_HOME,
  ACT_JUMP_CHAPTER,
  ACT_JUMP_BOOKMARK,
  ACT_CYCLE_REFRESH,
  ACT_CYCLE_WALLPAPER,
  ACT_WIFI_TRANSFER,  // start the AP and jump straight to its QR — the normal transfer path
  ACT_WIFI_CLOSE_AP,
  ACT_WIFI_SHOW_QR,
  ACT_WIFI_ON,
  ACT_WIFI_OFF,
  ACT_START_GAME,
  ACT_DELETE_BOOK,
  ACT_CONFIRM_DELETE,
};

std::vector<Row> homeRows() {
  std::vector<Row> rows;
  if (activeBook >= 0) {
    BookMeta& b = library::book(activeBook);
    rows.push_back({ListRow("Continue - " + bookLabel(b)), ACT_CONTINUE_ACTIVE});
    rows.push_back({ListRow("Bookmark this page"), ACT_BOOKMARK_HERE});
    if (!b.bookmarks.empty()) {
      rows.push_back({ListRow("Bookmarks (" + String(b.bookmarks.size()) + ")"), ACT_OPEN_BOOKMARKS});
    }
  }
  rows.push_back({ListRow("Library"), ACT_OPEN_LIBRARY});
  // One press, no prior Wi-Fi state needed: turns the AP on and jumps straight to its QR,
  // skipping the old two-step "open Wi-Fi screen, then turn it on" flow. Only relevant
  // when the AP isn't already the state you're in — otherwise the Wi-Fi menu (below) is
  // where you'd want to be to see the code again or turn it off.
  if (!wifi_setup::apOpen()) {
    rows.push_back({ListRow("Connect / transfer books"), ACT_WIFI_TRANSFER});
  }
  rows.push_back({ListRow("Reading stats"), ACT_OPEN_ALL_STATS});
  rows.push_back({ListRow("Games"), ACT_OPEN_GAMES});
  rows.push_back({ListRow("Settings"), ACT_OPEN_SETTINGS});
  rows.push_back({ListRow("Wi-Fi"), ACT_OPEN_WIFI});
  return rows;
}

std::vector<Row> libraryRows() {
  std::vector<Row> rows;
  if (library::count() == 0) {
    rows.push_back({ListRow("(no books - upload over Wi-Fi)"), ACT_NONE});
    return rows;
  }
  for (size_t i = 0; i < library::count(); i++) {
    BookMeta& b = library::book(i);
    rows.push_back({ListRow(bookLabel(b)), ACT_PICK_BOOK, (int)i});
    String meta = b.author;
    if (meta.length()) meta += " - ";
    meta += String(b.chapterCount) + " ch";
    rows.push_back({ListRow(meta, false, true), ACT_NONE});
    if (i + 1 < library::count()) rows.push_back({ListRow::sep(), ACT_NONE});
  }
  return rows;
}

std::vector<Row> bookMenuRows() {
  std::vector<Row> rows;
  if (browseBook < 0 || browseBook >= (int)library::count()) {
    rows.push_back({ListRow("(no book selected)"), ACT_NONE});
    return rows;
  }
  BookMeta& b = library::book(browseBook);
  if (browseBook == activeBook) {
    rows.push_back({ListRow("Bookmark this page"), ACT_BOOKMARK_HERE});
  } else {
    bool hasProgress = b.lastChapterIdx > 0 || b.lastPageIdx > 0;
    if (hasProgress) {
      rows.push_back({ListRow("Continue - Ch " + String(b.lastChapterIdx + 1) + ", p" +
                              String(b.lastPageIdx + 1)),
                      ACT_CONTINUE_BOOK});
    }
    rows.push_back({ListRow(hasProgress ? "Start from beginning" : "Start reading"), ACT_START_BOOK});
  }
  rows.push_back({ListRow("Browse chapters"), ACT_OPEN_CHAPTERS});
  if (!b.bookmarks.empty()) {
    rows.push_back({ListRow("Bookmarks (" + String(b.bookmarks.size()) + ")"), ACT_OPEN_BOOKMARKS});
  }
  rows.push_back({ListRow("Reading stats"), ACT_OPEN_STATS});
  // Can't delete the book you're currently reading from here — closing it out from under
  // an active BookReader isn't handled, and there's no scenario where you'd want to.
  if (browseBook != activeBook) {
    rows.push_back({ListRow::sep(), ACT_NONE});
    rows.push_back({ListRow("Delete book"), ACT_DELETE_BOOK});
  }
  rows.push_back({ListRow("More... (Library, Settings)"), ACT_OPEN_HOME});
  return rows;
}

std::vector<Row> confirmDeleteRows() {
  std::vector<Row> rows;
  if (browseBook < 0 || browseBook >= (int)library::count()) {
    rows.push_back({ListRow("(no book selected)"), ACT_NONE});
    return rows;
  }
  BookMeta& b = library::book(browseBook);
  rows.push_back({ListRow("Delete \"" + bookLabel(b) + "\"?", false, true), ACT_NONE});
  rows.push_back({ListRow("This can't be undone.", false, true), ACT_NONE});
  rows.push_back({ListRow::sep(), ACT_NONE});
  rows.push_back({ListRow("Yes, delete"), ACT_CONFIRM_DELETE});
  return rows;
}

std::vector<Row> chapterRows() {
  std::vector<Row> rows;
  // Chapter titles live in the book file, so this needs it open. Browsing another book's
  // chapters opens it read-only for the duration of the screen.
  if (browseBook < 0 || browseBook >= (int)library::count()) {
    rows.push_back({ListRow("(no book selected)"), ACT_NONE});
    return rows;
  }
  if (browseBook == activeBook && currentBook.isOpen()) {
    for (size_t i = 0; i < currentBook.chapterCount(); i++) {
      rows.push_back({ListRow(currentBook.chapter(i).title), ACT_JUMP_CHAPTER, (int)i});
    }
    return rows;
  }
  BookReader peek;
  if (peek.open(LittleFS, library::book(browseBook).path.c_str())) {
    for (size_t i = 0; i < peek.chapterCount(); i++) {
      rows.push_back({ListRow(peek.chapter(i).title), ACT_JUMP_CHAPTER, (int)i});
    }
    peek.close();
  }
  if (rows.empty()) rows.push_back({ListRow("(no chapters)"), ACT_NONE});
  return rows;
}

std::vector<Row> bookmarkRows() {
  std::vector<Row> rows;
  if (browseBook < 0 || browseBook >= (int)library::count()) {
    rows.push_back({ListRow("(no book selected)"), ACT_NONE});
    return rows;
  }
  BookMeta& b = library::book(browseBook);
  if (b.bookmarks.empty()) {
    rows.push_back({ListRow("(no bookmarks yet)"), ACT_NONE});
    return rows;
  }
  for (size_t i = 0; i < b.bookmarks.size(); i++) {
    rows.push_back({ListRow("Ch " + String(b.bookmarks[i].chapterIdx + 1) + " - p" +
                            String(b.bookmarks[i].pageIdx + 1)),
                    ACT_JUMP_BOOKMARK, (int)i});
  }
  return rows;
}

std::vector<Row> statsRows() {
  std::vector<Row> rows;
  if (browseBook < 0 || browseBook >= (int)library::count()) {
    rows.push_back({ListRow("(no book selected)"), ACT_NONE});
    return rows;
  }
  BookMeta& b = library::book(browseBook);
  library::Stats s = library::statsFor(browseBook);
  int pct = b.totalPages ? (int)((b.absolutePage() * 100L) / b.totalPages) : 0;
  rows.push_back({ListRow("Progress: " + String(b.absolutePage()) + "/" + String(b.totalPages) +
                          " pg (" + String(pct) + "%)"),
                  ACT_NONE});
  rows.push_back({ListRow("Today: " + String(s.pagesToday) + " pages"), ACT_NONE});
  if (s.daysLogged > 0) {
    rows.push_back({ListRow("Avg pace: " + String(s.avgPagesPerDay, 1) + " pg/day"), ACT_NONE});
  } else {
    rows.push_back({ListRow("Avg pace: no data yet"), ACT_NONE});
  }
  if (s.daysLeft >= 0) {
    rows.push_back({ListRow("Est. finish: ~" + String(s.daysLeft) + " days"), ACT_NONE});
  } else {
    rows.push_back({ListRow("Est. finish: keep reading"), ACT_NONE});
  }
  rows.push_back({ListRow("Time reading: " + fmtDuration(s.totalSeconds)), ACT_NONE});
  if (!library::timeIsSynced()) {
    rows.push_back({ListRow("(clock not set - no Wi-Fi yet)", false, true), ACT_NONE});
  }
  return rows;
}

// Whole-library stats, as opposed to statsRows()'s single book. Display rows only.
std::vector<Row> allStatsRows() {
  std::vector<Row> rows;
  library::OverallStats s = library::overallStats();
  rows.push_back({ListRow("Today: " + String(s.pagesToday) + " pg - " + fmtDuration(s.secondsToday)), ACT_NONE});
  if (s.daysLogged == 0) {
    rows.push_back({ListRow("No reading logged yet"), ACT_NONE});
    rows.push_back({ListRow("(turn a few pages)", false, true), ACT_NONE});
    return rows;
  }
  rows.push_back({ListRow("All time: " + String(s.totalPages) + " pg - " + fmtDuration(s.totalSeconds)), ACT_NONE});
  rows.push_back({ListRow("Avg pace: " + String(s.avgPagesPerDay, 1) + " pg/day"), ACT_NONE});
  if (s.streakDays > 0) {
    rows.push_back({ListRow("Streak: " + String(s.streakDays) + " day" + (s.streakDays == 1 ? "" : "s")), ACT_NONE});
  } else {
    rows.push_back({ListRow("Streak: none today"), ACT_NONE});
  }
  rows.push_back({ListRow("Days read: " + String(s.daysLogged)), ACT_NONE});
  rows.push_back({ListRow::sep(), ACT_NONE});
  rows.push_back({ListRow("Library: " + String(library::count()) + " book" + (library::count() == 1 ? "" : "s")), ACT_NONE});
  rows.push_back({ListRow("In progress: " + String(s.inProgress) + " - done: " + String(s.finished)), ACT_NONE});
  if (!library::timeIsSynced()) {
    rows.push_back({ListRow("(clock not set - no Wi-Fi yet)", false, true), ACT_NONE});
  }
  return rows;
}

std::vector<Row> settingsRows() {
  std::vector<Row> rows;
  String n = String(settings::refreshEveryNPages());
  if (n.length() < 2) n = " " + n;
  String refresh = "Full refresh every: " + n + " pages";
  if (editingRefresh) refresh += " <";
  rows.push_back({ListRow(refresh), ACT_CYCLE_REFRESH});
  String wp = "Wallpaper: ";
  int wpIdx = settings::wallpaperIndex();
  wp += (wpIdx >= 0 && wpIdx < (int)wallpaperFiles.size()) ? wallpaperFiles[wpIdx] : String("(none)");
  rows.push_back({ListRow(wp), ACT_CYCLE_WALLPAPER});
  // Layout constants are shown, not offered: they're compiled into config.h and baked into
  // every .cebk at conversion time, so changing one here would desync the page tables the
  // device draws from. docs/SIMULATOR.md covers why this differs from the simulator's sliders.
  rows.push_back({ListRow::sep(), ACT_NONE});
  rows.push_back({ListRow("Layout (fixed at conversion):", false, true), ACT_NONE});
  rows.push_back({ListRow("Font 8px, line " + String(LINE_HEIGHT_PX) + "px", false, true), ACT_NONE});
  rows.push_back({ListRow("Margin " + String(MARGIN_PX) + "px, heading " +
                          String(HEADING_LINE_HEIGHT_PX) + "px",
                          false, true),
                  ACT_NONE});
  return rows;
}

// The leading row is the current status, and status isn't something you can act on — it's
// marked unselectable so the cursor opens on the on/off toggle instead of making you
// scroll past a line that does nothing every single time.
std::vector<Row> wifiRows() {
  std::vector<Row> rows;
  // The AP is the normal book-transfer path now, not a one-time setup fallback — reflected
  // in "Show QR"/"Turn off" reading like ordinary Wi-Fi controls rather than an escape hatch.
  if (wifi_setup::apOpen()) {
    rows.push_back({ListRow(String("Transfer mode: ") + WIFI_AP_NAME, false), ACT_NONE});
    rows.push_back({ListRow("Show QR code"), ACT_WIFI_SHOW_QR});
    rows.push_back({ListRow("Turn off"), ACT_WIFI_CLOSE_AP});
    return rows;
  }
  if (wifi_setup::isConnected()) {
    rows.push_back({ListRow("On — " + wifi_setup::ipAddress(), false), ACT_NONE});
    rows.push_back({ListRow("Show QR code"), ACT_WIFI_SHOW_QR});
    rows.push_back({ListRow("Turn Wi-Fi off"), ACT_WIFI_OFF});
  } else if (wifi_setup::radioOn()) {
    rows.push_back({ListRow("Connecting...", false), ACT_NONE});
    rows.push_back({ListRow("Turn Wi-Fi off"), ACT_WIFI_OFF});
  } else {
    rows.push_back({ListRow("Off (saves battery)", false), ACT_NONE});
    rows.push_back({ListRow("Turn Wi-Fi on"), ACT_WIFI_ON});
  }
  rows.push_back({ListRow("Connect / transfer books"), ACT_WIFI_TRANSFER});
  return rows;
}

std::vector<Row> gamesRows() {
  std::vector<Row> rows;
  rows.push_back({ListRow("Lights Out"), ACT_START_GAME, (int)Game::LightsOut});
  rows.push_back({ListRow("Minesweeper"), ACT_START_GAME, (int)Game::Minesweeper});
  rows.push_back({ListRow("Sudoku"), ACT_START_GAME, (int)Game::Sudoku});
  rows.push_back({ListRow("Hangman"), ACT_START_GAME, (int)Game::Hangman});
  rows.push_back({ListRow("Tic-Tac-Toe"), ACT_START_GAME, (int)Game::TicTacToe});
  return rows;
}

std::vector<Row> buildRows() {
  switch (current()) {
    case Screen::Home: return homeRows();
    case Screen::Library: return libraryRows();
    case Screen::BookMenu: return bookMenuRows();
    case Screen::BookChapters: return chapterRows();
    case Screen::Bookmarks: return bookmarkRows();
    case Screen::Stats: return statsRows();
    case Screen::AllStats: return allStatsRows();
    case Screen::ConfirmDelete: return confirmDeleteRows();
    case Screen::Settings: return settingsRows();
    case Screen::WiFi: return wifiRows();
    case Screen::Games: return gamesRows();
    default: return std::vector<Row>();
  }
}

int selectableCount(const std::vector<Row>& rows) {
  int n = 0;
  for (const auto& r : rows) {
    if (r.row.selectable && !r.row.separator) n++;
  }
  return n;
}

void applyScroll(int delta) {
  if (delta == 0) return;
  Screen screen = current();
  if (screen == Screen::Reading) {
    int step = delta > 0 ? 1 : -1;
    int n = delta > 0 ? delta : -delta;
    bool moved = false;
    for (int i = 0; i < n; i++) {
      if (!skipReading(step)) break;
      moved = true;
    }
    if (moved) renderReading();
  } else if (screen == Screen::GameScreen) {
    games::scroll(delta);
    renderCurrent();
  } else {
    int count = selectableCount(buildRows());
    if (count > 0) {
      focus = std::max(0, std::min(focus + delta, count - 1));
      renderCurrent();
    }
  }
}

void renderCurrent() {
  if (asleep) return;
  switch (current()) {
    case Screen::Reading:
      renderReading();
      return;
    case Screen::GameScreen:
      rend->renderCustom([](Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) { games::draw(g, u8f); });
      return;
    case Screen::WifiQr:
      // Reads state at render time rather than at the point the screen was pushed, so
      // a QR shown right as the AP finished coming up is never stale.
      if (wifi_setup::apOpen()) {
        qr_screen::show(*rend, wifi_setup::apQrPayload(), wifi_setup::apCaption());
      } else if (wifi_setup::isConnected()) {
        qr_screen::show(*rend, wifi_setup::staQrPayload(), "Scan to open\nthe uploader");
      } else {
        // Reachable only if Wi-Fi dropped between pushing this screen and drawing it —
        // the AP payload is still a valid, working code, just not one anything is
        // currently advertising; EXIT backs out to the Wi-Fi menu to sort it out.
        qr_screen::show(*rend, wifi_setup::apQrPayload(), "Wi-Fi is off.\nPress EXIT.");
      }
      return;
    default: {
      std::vector<Row> rows = buildRows();
      std::vector<ListRow> listRows;
      listRows.reserve(rows.size());
      for (const auto& r : rows) listRows.push_back(r.row);
      int count = selectableCount(rows);
      if (count > 0) focus = std::max(0, std::min(focus, count - 1));
      rend->renderList(listRows, focus);
      return;
    }
  }
}

const Row* focusedRow(const std::vector<Row>& rows) {
  int n = 0;
  for (const auto& r : rows) {
    if (!r.row.selectable || r.row.separator) continue;
    if (n == focus) return &r;
    n++;
  }
  return nullptr;
}

void activateFocused() {
  std::vector<Row> rows = buildRows();
  const Row* row = focusedRow(rows);
  if (!row) return;
  switch (row->action) {
    case ACT_CONTINUE_ACTIVE:
      goToReading();
      break;
    case ACT_BOOKMARK_HERE:
      if (activeBook >= 0) library::toggleBookmark(activeBook, chapterIdx, pageIdx);
      break;
    case ACT_OPEN_BOOKMARKS:
      if (browseBook < 0) browseBook = activeBook;
      pushScreen(Screen::Bookmarks);
      break;
    case ACT_OPEN_LIBRARY: pushScreen(Screen::Library); break;
    case ACT_OPEN_GAMES: pushScreen(Screen::Games); break;
    case ACT_OPEN_SETTINGS: pushScreen(Screen::Settings); break;
    case ACT_OPEN_WIFI: pushScreen(Screen::WiFi); break;
    case ACT_OPEN_HOME: pushScreen(Screen::Home); break;
    case ACT_OPEN_CHAPTERS: pushScreen(Screen::BookChapters); break;
    case ACT_OPEN_STATS: pushScreen(Screen::Stats); break;
    case ACT_OPEN_ALL_STATS: pushScreen(Screen::AllStats); break;
    case ACT_DELETE_BOOK: pushScreen(Screen::ConfirmDelete); break;
    case ACT_CONFIRM_DELETE:
      if (browseBook >= 0 && browseBook != activeBook) library::remove((size_t)browseBook);
      browseBook = -1;
      popScreen();  // ConfirmDelete -> BookMenu
      popScreen();  // BookMenu -> Library (the book it was about no longer exists)
      break;
    case ACT_PICK_BOOK:
      browseBook = row->param;
      pushScreen(Screen::BookMenu);
      break;
    case ACT_CONTINUE_BOOK: {
      BookMeta& b = library::book(browseBook);
      if (openBook(browseBook, b.lastChapterIdx, b.lastPageIdx)) goToReading();
      break;
    }
    case ACT_START_BOOK:
      if (openBook(browseBook, 0, 0)) goToReading();
      break;
    case ACT_JUMP_CHAPTER:
      if (browseBook != activeBook) {
        if (!openBook(browseBook, (uint16_t)row->param, 0)) break;
      } else {
        chapterIdx = (uint16_t)row->param;
        pageIdx = 0;
        library::setPosition(activeBook, chapterIdx, pageIdx);
      }
      goToReading();
      break;
    case ACT_JUMP_BOOKMARK: {
      BookMeta& b = library::book(browseBook);
      if (row->param < 0 || row->param >= (int)b.bookmarks.size()) break;
      Bookmark m = b.bookmarks[row->param];
      if (browseBook != activeBook) {
        if (!openBook(browseBook, m.chapterIdx, m.pageIdx)) break;
      } else {
        chapterIdx = m.chapterIdx;
        pageIdx = m.pageIdx;
        library::setPosition(activeBook, chapterIdx, pageIdx);
      }
      goToReading();
      break;
    }
    case ACT_CYCLE_REFRESH: {
      int n = settings::refreshEveryNPages() + 1;
      if (n > settings::REFRESH_MAX) n = settings::REFRESH_MIN;
      settings::setRefreshEveryNPages(n);
      break;
    }
    case ACT_CYCLE_WALLPAPER: {
      int total = (int)wallpaperFiles.size();
      if (total == 0) break;
      int next = settings::wallpaperIndex() + 1;
      if (next >= total) next = -1;  // -1 = none, so "no wallpaper" stays reachable
      settings::setWallpaperIndex(next);
      break;
    }
    case ACT_WIFI_ON:
      wifi_setup::enable();
      break;
    case ACT_WIFI_OFF:
      wifi_setup::disable();
      break;
    case ACT_WIFI_TRANSFER:
      wifi_setup::startAP();
      pushScreen(Screen::WifiQr);
      break;
    case ACT_WIFI_SHOW_QR:
      pushScreen(Screen::WifiQr);
      break;
    case ACT_WIFI_CLOSE_AP:
      wifi_setup::stopAP();
      break;
    case ACT_START_GAME:
      games::start((Game)row->param);
      pushScreen(Screen::GameScreen);
      break;
    default:
      break;
  }
  renderCurrent();
}

// ---------------------------------------------------------------- sleep

void goToSleep() {
  if (asleep) return;
  asleep = true;
  library::flush();
  wifi_setup::disable();
  String title = "ROTARY READER";
  String extra;
  if (activeBook >= 0) {
    BookMeta& b = library::book(activeBook);
    title = bookLabel(b);
    if (b.totalPages) {
      int pct = (int)((b.absolutePage() * 100L) / b.totalPages);
      extra = String(pct) + "%";
    }
  }
  rend->renderSleep(title, extra, loadSelectedWallpaper());
  rend->powerDown();
  // EXIT-hold is still down here; wait for release or light-sleep would wake immediately.
  input::blockUntilWake();
  rend->wakeDisplay();
  asleep = false;
  lastInputMs = millis();
  rend->forceFullRefreshNext();
  renderCurrent();
}

void wake() {
  if (!asleep) return;
  asleep = false;
  rend->wakeDisplay();
  rend->forceFullRefreshNext();
  renderCurrent();
}

}  // namespace

void ui::begin(Renderer* renderer) {
  rend = renderer;
  scanWallpapers();
  // Empty scan used to force index -1 into NVS, so a later successful scan still showed
  // "(none)" until the user cycled Settings. If we have files, select the first unless
  // the saved index already points at one of them.
  if (!wallpaperFiles.empty()) {
    int idx = settings::wallpaperIndex();
    if (idx < 0 || idx >= (int)wallpaperFiles.size()) settings::setWallpaperIndex(0);
  }

  // Land where the reader left off. The saved active book is the real answer; the scan
  // below is only a fallback for state written before it was recorded. It used to be the
  // *whole* answer, which is why booting with two started books opened whichever one won
  // the title sort rather than the one actually being read.
  int resume = library::activeBookIndex();
  if (resume < 0) {
    for (size_t i = 0; i < library::count(); i++) {
      BookMeta& b = library::book(i);
      if (b.lastChapterIdx > 0 || b.lastPageIdx > 0) {
        resume = (int)i;
        break;
      }
    }
  }
  if (resume < 0 && library::count() > 0) resume = 0;
  if (resume >= 0) {
    BookMeta& b = library::book(resume);
    if (openBook(resume, b.lastChapterIdx, b.lastPageIdx)) {
      goToReading();
    }
  }
  if (stack.empty()) {
    stack.push_back(Screen::Home);
    stack.push_back(Screen::Library);
  }
  lastInputMs = millis();
  lastReadingTickMs = millis();
  lastFlushMs = millis();
  renderCurrent();
}

void ui::showMessage(const String& line) {
  if (rend) rend->renderStatusLine(line);
}

void ui::onLibraryChanged() {
  library::rescan();
  if (!asleep && current() == Screen::Library) renderCurrent();
}

void ui::onWallpapersChanged() {
  scanWallpapers();
  if (!wallpaperFiles.empty()) {
    int idx = settings::wallpaperIndex();
    if (idx < 0 || idx >= (int)wallpaperFiles.size()) settings::setWallpaperIndex(0);
  } else {
    settings::setWallpaperIndex(-1);
  }
  if (!asleep && current() == Screen::Settings) renderCurrent();
}

void ui::handleScroll(int delta) {
  if (delta == 0) return;
  lastInputMs = millis();
  if (asleep) {
    wake();
    return;
  }
  if (editingRefresh) {
    settings::setRefreshEveryNPages(settings::refreshEveryNPages() + delta);
    renderCurrent();
    return;
  }
  applyScroll(delta);
}

void ui::handle(InputEvent e) {
  if (e == InputEvent::None) return;
  if (e == InputEvent::ScrollNext) {
    handleScroll(1);
    return;
  }
  if (e == InputEvent::ScrollPrev) {
    handleScroll(-1);
    return;
  }
  lastInputMs = millis();
  // A real e-reader doesn't act on the press that woke it, and neither does the simulator.
  if (asleep) {
    wake();
    return;
  }

  // Scrub-edit session on the refresh-cadence setting: the dial adjusts the value, MENU
  // commits, EXIT reverts. Same contract as the simulator's tryEnterEditMode().
  if (editingRefresh) {
    switch (e) {
      case InputEvent::ScrollNext:
        settings::setRefreshEveryNPages(settings::refreshEveryNPages() + 1);
        break;
      case InputEvent::ScrollPrev:
        settings::setRefreshEveryNPages(settings::refreshEveryNPages() - 1);
        break;
      case InputEvent::MenuShort:
      case InputEvent::ConfShort:
        editingRefresh = false;
        break;
      case InputEvent::ExitShort:
        settings::setRefreshEveryNPages(editOriginal);
        editingRefresh = false;
        break;
      default:
        break;
    }
    renderCurrent();
    return;
  }

  Screen screen = current();
  switch (e) {
    case InputEvent::MenuShort:
      if (screen == Screen::Reading) {
        browseBook = activeBook;
        pushScreen(Screen::BookMenu);
        renderCurrent();
      } else if (screen == Screen::GameScreen) {
        games::primary();
        renderCurrent();
      } else {
        activateFocused();
      }
      break;
    case InputEvent::ConfShort:
      if (screen == Screen::GameScreen) {
        games::secondary();  // Minesweeper flags, Sudoku cycles down, others act like MENU
        renderCurrent();
      } else if (screen == Screen::Reading) {
        browseBook = activeBook;
        pushScreen(Screen::BookMenu);
        renderCurrent();
      } else {
        activateFocused();
      }
      break;
    case InputEvent::ConfLong:
      if (screen == Screen::Settings) {
        const std::vector<Row> rows = settingsRows();
        const Row* row = focusedRow(rows);
        if (row && row->action == ACT_CYCLE_REFRESH) {
          editingRefresh = true;
          editOriginal = settings::refreshEveryNPages();
          renderCurrent();
          break;
        }
      }
      if (screen != Screen::GameScreen && screen != Screen::Reading) {
        activateFocused();
      }
      break;
    case InputEvent::MenuLong:
      rend->forceFullRefreshNext();
      renderCurrent();
      break;
    case InputEvent::ExitShort:
      // The AP started from the Home/Wi-Fi menu is just another screen on the stack —
      // EXIT backs out of it normally (WifiQr -> WiFi list -> ... ), same as everywhere
      // else, and "Turn off" is the deliberate way to actually close it from there. This
      // universal close is only for the MENU-held-at-boot AP (main.cpp), which bypasses
      // the screen stack entirely, so there's nothing here for a normal pop to land on.
      if (wifi_setup::apOpen() && screen != Screen::WifiQr && screen != Screen::WiFi) {
        wifi_setup::stopAP();
        renderCurrent();
        break;
      }
      if (screen == Screen::Reading) {
        pushScreen(Screen::Home);
      } else {
        popScreen();
      }
      renderCurrent();
      break;
    case InputEvent::ExitLong:
      if (wifi_setup::apOpen() && screen != Screen::WifiQr && screen != Screen::WiFi) {
        wifi_setup::stopAP();
        renderCurrent();
        break;
      }
      goToSleep();
      break;
    default:
      break;
  }
}

void ui::tick() {
  unsigned long now = millis();

  // Reading-time accounting: only counts while a book is actually on screen and awake —
  // not in menus, not in a game, not asleep. Mirrors the simulator's heartbeat.
  if (!asleep && current() == Screen::Reading && activeBook >= 0) {
    if (now - lastReadingTickMs >= READING_TICK_MS) {
      library::logReadingSeconds(activeBook, (uint32_t)((now - lastReadingTickMs) / 1000));
      lastReadingTickMs = now;
    }
  } else {
    lastReadingTickMs = now;
  }

  if (!asleep && current() == Screen::WiFi) {
    bool conn = wifi_setup::isConnected();
    bool ap = wifi_setup::apOpen();
    if (conn != lastWifiConnected || ap != lastApOpen) {
      lastWifiConnected = conn;
      lastApOpen = ap;
      renderCurrent();
    }
  } else {
    lastWifiConnected = wifi_setup::isConnected();
    lastApOpen = wifi_setup::apOpen();
  }

  // Debounced persistence: page turns mark state dirty, this writes it out once the keys
  // have been quiet for a moment. Runs before the sleep block below so whatever we doze
  // through is already on flash.
  if (library::isDirty() && now - lastInputMs > FLUSH_DEBOUNCE_MS &&
      now - lastFlushMs > FLUSH_DEBOUNCE_MS) {
    lastFlushMs = now;
    library::flush();
  }

  // Never doze or show the sleep screen while the AP is up (a transfer may be in flight)
  // or a physical key is mid-press.
  if (asleep || wifi_setup::apOpen() || input::anyButtonDown()) return;

  unsigned long idle = now - lastInputMs;

  // Stage 2 first: long enough with no key at all and the sleep screen goes up.
  if (idle > IDLE_SLEEP_SCREEN_MS) {
    goToSleep();
    return;
  }

  // Stage 1: doze between keystrokes. Skipped while the radio is up, because light sleep
  // stalls the web server mid-upload — a device you're uploading to is a device someone is
  // actively using, so there's nothing to save there anyway.
  if (wifi_setup::radioOn()) return;
  unsigned long quiet =
      (current() == Screen::Reading) ? IDLE_LIGHT_SLEEP_READING_MS : IDLE_LIGHT_SLEEP_MENU_MS;
  if (idle < quiet) return;

  // Wake no later than the sleep-screen deadline, so it still fires on time from a doze —
  // this is the "timer that keeps running while asleep" that makes stage 2 reachable
  // without polling for ten minutes.
  input::dozeUntilInput(IDLE_SLEEP_SCREEN_MS - idle);
}
