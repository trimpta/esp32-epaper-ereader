#include "ui.h"

#include "book_format.h"
#include "config.h"
#include "games.h"
#include "library.h"
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

unsigned long lastInputMs = 0;
unsigned long lastReadingTickMs = 0;
unsigned long lastFlushMs = 0;
const unsigned long READING_TICK_MS = 15000;  // stats granularity; also bounds flash writes
const unsigned long FLUSH_IDLE_MS = 30000;

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
    String name = f.name();
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    bool sized = f.size() == WALLPAPER_BYTES;  // raw 1-bit panel-native bitmap, nothing else
    f.close();
    if (sized) wallpaperFiles.push_back(name);
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
    return;  // already at the last page of the last chapter
  }
  library::setPosition(activeBook, chapterIdx, pageIdx);
  library::logPageTurn(activeBook);  // forward turns only — re-reading isn't progress
  renderReading();
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
}

void popScreen() {
  if (stack.size() > 1) {
    stack.pop_back();
    focus = 0;
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
  ACT_WIFI_PORTAL,
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
  String refresh = "Full refresh every: " + String(settings::refreshEveryNPages()) + " pages";
  if (editingRefresh) refresh = "> " + refresh + " <";
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

std::vector<Row> wifiRows() {
  std::vector<Row> rows;
  if (wifi_setup::isConnected()) {
    rows.push_back({ListRow("Connected"), ACT_NONE});
    rows.push_back({ListRow("IP: " + wifi_setup::ipAddress()), ACT_NONE});
  } else {
    rows.push_back({ListRow("Not connected"), ACT_NONE});
  }
  rows.push_back({ListRow("Re-enter setup (AP mode)"), ACT_WIFI_PORTAL});
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

void renderCurrent() {
  if (asleep) return;
  switch (current()) {
    case Screen::Reading:
      renderReading();
      return;
    case Screen::GameScreen:
      rend->renderCustom([](Adafruit_GFX& g, U8G2_FOR_ADAFRUIT_GFX& u8f) { games::draw(g, u8f); });
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
    case ACT_WIFI_PORTAL:
      ui::showMessage("Join " + String(WIFI_AP_NAME));
      wifi_setup::startPortal();
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
  String title = activeBook >= 0 ? bookLabel(library::book(activeBook)) : String("ROTARY READER");
  rend->renderSleep(title, loadSelectedWallpaper());
  rend->powerDown();
}

void wake() {
  if (!asleep) return;
  asleep = false;
  rend->forceFullRefreshNext();  // clear whatever the sleep screen left behind
  renderCurrent();
}

}  // namespace

void ui::begin(Renderer* renderer) {
  rend = renderer;
  scanWallpapers();
  if (settings::wallpaperIndex() >= (int)wallpaperFiles.size()) settings::setWallpaperIndex(-1);

  // Land where the reader left off: the last book with saved progress, else the library.
  int resume = -1;
  for (size_t i = 0; i < library::count(); i++) {
    BookMeta& b = library::book(i);
    if (b.lastChapterIdx > 0 || b.lastPageIdx > 0) {
      resume = (int)i;
      break;
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

void ui::handle(InputEvent e) {
  if (e == InputEvent::None) return;
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
    case InputEvent::ScrollNext:
    case InputEvent::ScrollPrev: {
      int delta = (e == InputEvent::ScrollNext) ? 1 : -1;
      if (screen == Screen::Reading) {
        delta > 0 ? nextPage() : prevPage();
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
      break;
    }
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
      // Only the refresh cadence is scrub-editable; every other row is a name to cycle or
      // an inert readout, exactly as in the simulator (the Wallpaper row has no `.el`).
      if (screen == Screen::Settings) {
        const std::vector<Row> rows = settingsRows();
        const Row* row = focusedRow(rows);
        if (row && row->action == ACT_CYCLE_REFRESH) {
          editingRefresh = true;
          editOriginal = settings::refreshEveryNPages();
          renderCurrent();
        }
      }
      break;
    case InputEvent::MenuLong:
      rend->forceFullRefreshNext();
      renderCurrent();
      break;
    case InputEvent::ExitShort:
      if (screen == Screen::Reading) {
        pushScreen(Screen::Home);
      } else {
        popScreen();
      }
      renderCurrent();
      break;
    case InputEvent::ExitLong:
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

  if (!asleep) {
    unsigned long idleLimit =
        (current() == Screen::Reading) ? IDLE_SLEEP_READING_MS : IDLE_SLEEP_MENU_MS;
    if (now - lastInputMs > idleLimit && !input::anyButtonDown()) goToSleep();
  }

  // Debounced persistence: page turns mark state dirty, this writes it out at most every
  // 30s of quiet, so a reading session isn't one flash write per page turn.
  if (now - lastFlushMs > FLUSH_IDLE_MS) {
    lastFlushMs = now;
    library::flush();
  }
}
