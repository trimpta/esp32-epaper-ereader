#pragma once
// The library data model: what's in /books, plus the small amount of per-book state the
// reader accumulates (resume position, bookmarks, reading stats).
//
// The simulator keeps the equivalent in localStorage keyed by title+author; on device the
// book file itself is already sitting in flash, so state is keyed by path and persisted to
// one small JSON file (docs/FLASH_BUDGET.md: "a small file per book in LittleFS ... a
// storage backend that needs picking"). Writes are debounced — a page turn marks the state
// dirty, and it's flushed on sleep/menu-open/idle rather than on every turn, because
// flash write cycles are the one resource a page-turn-frequency write would actually burn.

#include <Arduino.h>
#include <vector>

struct Bookmark {
  uint16_t chapterIdx = 0;
  uint16_t pageIdx = 0;
};

// One calendar day of reading for one book. `day` is days-since-epoch (UTC), or 0 when the
// clock hasn't been set yet — the device has no RTC, so time comes from NTP after WiFi
// connects and everything read before that lands in the day-0 bucket rather than being
// silently attributed to the wrong date.
struct DayStat {
  uint32_t day = 0;
  uint16_t pages = 0;
  uint32_t seconds = 0;
};

struct BookMeta {
  String path;
  String title;
  String author;
  uint16_t chapterCount = 0;
  uint16_t totalPages = 0;
  uint16_t lastChapterIdx = 0;
  uint16_t lastPageIdx = 0;
  std::vector<Bookmark> bookmarks;
  std::vector<DayStat> days;

  uint16_t absolutePage() const;  // 1-based page number across the whole book
};

namespace library {
void begin();   // scans /books and loads saved state
void rescan();  // after an upload — keeps state for books that are still present
bool addPlainNote(const String& title, const String& author, const String& body);

size_t count();
BookMeta& book(size_t idx);
bool remove(size_t idx);  // deletes the .cebk from LittleFS and rescans; false if idx is invalid

void setPosition(size_t idx, uint16_t chapterIdx, uint16_t pageIdx);

// Which book the reader was last actually reading. Persisted by path, because "the first
// book with any progress" (what ui::begin used to guess) is a title-sort artifact, not a
// memory: two books with saved positions made the boot land in whichever one sorted first.
void setActiveBook(int idx);   // -1 = none
int activeBookIndex();         // -1 if nothing was saved, or that book is no longer present

bool toggleBookmark(size_t idx, uint16_t chapterIdx, uint16_t pageIdx);  // true if added

void logPageTurn(size_t idx);                     // forward turns only — see docs/SIMULATOR.md
void logReadingSeconds(size_t idx, uint32_t sec);

// Aggregates for the Reading stats screen.
struct Stats {
  uint16_t pagesToday = 0;
  uint32_t totalSeconds = 0;
  uint16_t daysLogged = 0;
  float avgPagesPerDay = 0;
  int daysLeft = -1;  // -1 = not enough data yet
};
Stats statsFor(size_t idx);

// Everything across every book, for the Home > Reading stats screen. Distinct from
// statsFor(), which answers "how is this one book going" — this answers "how much am I
// reading", and so merges each book's per-day buckets into one timeline first.
struct OverallStats {
  uint16_t pagesToday = 0;
  uint32_t secondsToday = 0;
  uint32_t totalPages = 0;
  uint32_t totalSeconds = 0;
  uint16_t daysLogged = 0;
  float avgPagesPerDay = 0;
  uint16_t streakDays = 0;   // consecutive days ending today (or yesterday, if today is unread)
  uint16_t inProgress = 0;
  uint16_t finished = 0;
};
OverallStats overallStats();

void markDirty();
bool isDirty();     // true when there are unsaved changes — see ui::tick's flush debounce
void flush();       // writes if dirty
bool timeIsSynced();
}  // namespace library
