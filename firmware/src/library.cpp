#include "library.h"
#include "book_format.h"
#include "config.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <algorithm>
#include <math.h>
#include <time.h>

namespace {

std::vector<BookMeta> books;
bool dirty = false;

const size_t MAX_DAYS_KEPT = 14;  // bounded so the state file can't grow without limit

uint32_t todayIndex() {
  time_t now = time(nullptr);
  // Anything before 2023 means the clock was never set (no RTC on this board; time comes
  // from NTP after WiFi connects).
  if (now < 1672531200) return 0;
  return (uint32_t)(now / 86400);
}

DayStat& todayBucket(BookMeta& b) {
  uint32_t d = todayIndex();
  for (auto& s : b.days) {
    if (s.day == d) return s;
  }
  DayStat fresh;
  fresh.day = d;
  b.days.push_back(fresh);
  if (b.days.size() > MAX_DAYS_KEPT) {
    // Drop the oldest days. Note today's bucket isn't necessarily last after this sort —
    // when the clock hasn't been set it has day 0, which sorts *first* — so find it again
    // rather than assuming back() is the one just added.
    std::sort(b.days.begin(), b.days.end(),
              [](const DayStat& a, const DayStat& c) { return a.day < c.day; });
    b.days.erase(b.days.begin(), b.days.begin() + (b.days.size() - MAX_DAYS_KEPT));
    for (auto& s : b.days) {
      if (s.day == d) return s;
    }
    b.days.push_back(fresh);  // pruned away (only possible if d is the oldest) — re-add
  }
  return b.days.back();
}

bool endsWithCebk(const String& name) {
  return name.length() > 5 && name.endsWith(".cebk");
}

// Reads just the header + chapter table of each book (a few KB), not its text.
void scanBooks(std::vector<BookMeta>& out) {
  File dir = LittleFS.open(BOOKS_DIR);
  if (!dir || !dir.isDirectory()) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) {
      f.close();
      continue;
    }
    String name = f.name();
    // ESP32 core >= 2.0 returns a bare filename here, older cores returned a full path —
    // handle both rather than building "/books//books/x.cebk" on one of them.
    int slash = name.lastIndexOf('/');
    if (slash >= 0) name = name.substring(slash + 1);
    f.close();
    if (!endsWithCebk(name)) continue;  // skip partial uploads and stray files

    String path = String(BOOKS_DIR) + "/" + name;
    BookReader reader;
    if (!reader.open(LittleFS, path.c_str())) continue;  // corrupt/unreadable: leave it out
    BookMeta m;
    m.path = path;
    m.title = reader.title().length() ? reader.title() : name;
    m.author = reader.author();
    m.chapterCount = (uint16_t)reader.chapterCount();
    uint32_t total = 0;
    for (size_t i = 0; i < reader.chapterCount(); i++) total += reader.chapter(i).pageCount;
    m.totalPages = (uint16_t)std::min<uint32_t>(total, 65535);
    reader.close();
    out.push_back(m);
  }
  dir.close();
  std::sort(out.begin(), out.end(),
            [](const BookMeta& a, const BookMeta& b) { return a.title < b.title; });
}

void loadState() {
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return;  // corrupt state is not worth failing a boot over — start fresh

  JsonObject saved = doc["books"].as<JsonObject>();
  if (saved.isNull()) return;
  for (auto& b : books) {
    JsonObject o = saved[b.path].as<JsonObject>();
    if (o.isNull()) continue;
    b.lastChapterIdx = o["c"] | 0;
    b.lastPageIdx = o["p"] | 0;
    if (b.chapterCount && b.lastChapterIdx >= b.chapterCount) b.lastChapterIdx = 0;
    for (JsonArray bm : o["bm"].as<JsonArray>()) {
      Bookmark m;
      m.chapterIdx = bm[0] | 0;
      m.pageIdx = bm[1] | 0;
      if (b.chapterCount && m.chapterIdx < b.chapterCount) b.bookmarks.push_back(m);
    }
    for (JsonArray d : o["days"].as<JsonArray>()) {
      DayStat s;
      s.day = d[0] | 0;
      s.pages = d[1] | 0;
      s.seconds = d[2] | 0;
      b.days.push_back(s);
    }
  }
}

}  // namespace

uint16_t BookMeta::absolutePage() const {
  // Cheap approximation of "pages read": the exact per-chapter page counts live in the
  // .cebk chapter table, but re-opening the book just to draw a stats screen isn't worth
  // it, so this assumes an even spread across chapters when the book isn't the open one.
  if (chapterCount == 0 || totalPages == 0) return (uint16_t)(lastPageIdx + 1);
  uint32_t perChapter = totalPages / chapterCount;
  return (uint16_t)std::min<uint32_t>((uint32_t)lastChapterIdx * perChapter + lastPageIdx + 1,
                                       totalPages);
}

void library::begin() {
  if (!LittleFS.exists(BOOKS_DIR)) LittleFS.mkdir(BOOKS_DIR);
  books.clear();
  scanBooks(books);
  loadState();
}

void library::rescan() {
  std::vector<BookMeta> fresh;
  scanBooks(fresh);
  // Carry over state for books that survived the rescan, so an upload doesn't reset
  // anyone's reading position.
  for (auto& n : fresh) {
    for (auto& old : books) {
      if (old.path != n.path) continue;
      n.lastChapterIdx = old.lastChapterIdx;
      n.lastPageIdx = old.lastPageIdx;
      n.bookmarks = old.bookmarks;
      n.days = old.days;
      break;
    }
  }
  books.swap(fresh);
  markDirty();
}

size_t library::count() {
  return books.size();
}

BookMeta& library::book(size_t idx) {
  static BookMeta empty;
  if (idx >= books.size()) return empty;
  return books[idx];
}

void library::setPosition(size_t idx, uint16_t chapterIdx, uint16_t pageIdx) {
  if (idx >= books.size()) return;
  books[idx].lastChapterIdx = chapterIdx;
  books[idx].lastPageIdx = pageIdx;
  markDirty();
}

bool library::toggleBookmark(size_t idx, uint16_t chapterIdx, uint16_t pageIdx) {
  if (idx >= books.size()) return false;
  auto& marks = books[idx].bookmarks;
  for (size_t i = 0; i < marks.size(); i++) {
    if (marks[i].chapterIdx == chapterIdx && marks[i].pageIdx == pageIdx) {
      marks.erase(marks.begin() + i);
      markDirty();
      return false;
    }
  }
  Bookmark m;
  m.chapterIdx = chapterIdx;
  m.pageIdx = pageIdx;
  marks.push_back(m);
  markDirty();
  return true;
}

void library::logPageTurn(size_t idx) {
  if (idx >= books.size()) return;
  DayStat& s = todayBucket(books[idx]);
  if (s.pages < 65535) s.pages++;
  markDirty();
}

void library::logReadingSeconds(size_t idx, uint32_t sec) {
  if (idx >= books.size() || sec == 0) return;
  todayBucket(books[idx]).seconds += sec;
  markDirty();
}

library::Stats library::statsFor(size_t idx) {
  Stats out;
  if (idx >= books.size()) return out;
  BookMeta& b = books[idx];
  uint32_t today = todayIndex();
  uint32_t totalPages = 0;
  for (const auto& s : b.days) {
    out.totalSeconds += s.seconds;
    totalPages += s.pages;
    if (s.day == today) out.pagesToday = s.pages;
  }
  out.daysLogged = (uint16_t)b.days.size();
  // Averaged over days with logged activity, not calendar days since the book was opened —
  // taking a week off shouldn't drag the pace down. Matches the simulator's bookReadingStats().
  if (out.daysLogged > 0) out.avgPagesPerDay = (float)totalPages / (float)out.daysLogged;
  if (out.avgPagesPerDay > 0 && b.totalPages > 0) {
    int remaining = (int)b.totalPages - (int)b.absolutePage();
    if (remaining < 0) remaining = 0;
    out.daysLeft = (int)ceilf((float)remaining / out.avgPagesPerDay);
  }
  return out;
}

library::OverallStats library::overallStats() {
  OverallStats out;
  uint32_t today = todayIndex();

  // Merge every book's per-day buckets into one timeline. Day 0 is the "clock wasn't set"
  // bucket (see todayIndex) — it counts toward totals, since the reading happened, but it
  // can't take part in the streak, which needs real dates.
  std::vector<DayStat> merged;
  for (const auto& b : books) {
    for (const auto& s : b.days) {
      bool found = false;
      for (auto& m : merged) {
        if (m.day != s.day) continue;
        m.pages += s.pages;
        m.seconds += s.seconds;
        found = true;
        break;
      }
      if (!found) merged.push_back(s);
    }
  }

  for (const auto& m : merged) {
    out.totalPages += m.pages;
    out.totalSeconds += m.seconds;
    if (m.day == today && today != 0) {
      out.pagesToday = m.pages;
      out.secondsToday = m.seconds;
    }
  }
  out.daysLogged = (uint16_t)merged.size();
  if (out.daysLogged > 0) out.avgPagesPerDay = (float)out.totalPages / (float)out.daysLogged;

  // Walk back a day at a time from today. If today has nothing yet, start from yesterday,
  // so a streak isn't declared broken until a full day has actually been missed.
  if (today != 0) {
    auto hasDay = [&merged](uint32_t d) {
      for (const auto& m : merged) {
        if (m.day == d && m.pages > 0) return true;
      }
      return false;
    };
    uint32_t cursor = today;
    if (!hasDay(cursor) && cursor > 0) cursor--;
    while (cursor > 0 && hasDay(cursor)) {
      out.streakDays++;
      cursor--;
    }
  }

  for (const auto& b : books) {
    if (b.totalPages > 0 && b.absolutePage() >= b.totalPages) out.finished++;
    else if (b.lastChapterIdx > 0 || b.lastPageIdx > 0) out.inProgress++;
  }
  return out;
}

void library::markDirty() {
  dirty = true;
}

bool library::timeIsSynced() {
  return todayIndex() != 0;
}

void library::flush() {
  if (!dirty) return;
  dirty = false;

  JsonDocument doc;
  JsonObject saved = doc["books"].to<JsonObject>();
  for (const auto& b : books) {
    JsonObject o = saved[b.path].to<JsonObject>();
    o["c"] = b.lastChapterIdx;
    o["p"] = b.lastPageIdx;
    JsonArray bm = o["bm"].to<JsonArray>();
    for (const auto& m : b.bookmarks) {
      JsonArray e = bm.add<JsonArray>();
      e.add(m.chapterIdx);
      e.add(m.pageIdx);
    }
    JsonArray days = o["days"].to<JsonArray>();
    for (const auto& s : b.days) {
      JsonArray e = days.add<JsonArray>();
      e.add(s.day);
      e.add(s.pages);
      e.add(s.seconds);
    }
  }

  // Write to a temp file and rename, so losing power mid-write can't leave a truncated
  // state file that takes the whole library's positions with it.
  const char* tmp = "/state.tmp";
  File f = LittleFS.open(tmp, "w");
  if (!f) return;
  bool ok = serializeJson(doc, f) > 0;
  f.close();
  if (!ok) {
    LittleFS.remove(tmp);
    return;
  }
  LittleFS.remove(STATE_FILE);
  LittleFS.rename(tmp, STATE_FILE);
}
