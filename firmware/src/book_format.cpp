#include "book_format.h"
#include "config.h"

#include <algorithm>

// Every multi-byte read goes through these. They zero-fill first and record a short read
// in readOk_ rather than returning whatever happened to be on the stack: a truncated or
// corrupt .cebk used to produce indeterminate lengths/offsets here, which then became
// wild seeks and enormous vector reserves further up.
uint8_t BookReader::readU8() {
  uint8_t v = 0;
  if (file_.read(&v, 1) != 1) readOk_ = false;
  return v;
}

uint16_t BookReader::readU16() {
  uint8_t b[2] = {0, 0};
  if (file_.read(b, 2) != 2) readOk_ = false;
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

uint32_t BookReader::readU32() {
  uint8_t b[4] = {0, 0, 0, 0};
  if (file_.read(b, 4) != 4) readOk_ = false;
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

String BookReader::readLenPrefixedString() {
  uint8_t len = readU8();
  if (len == 0 || !readOk_) return String();
  std::vector<uint8_t> buf(len);
  if (file_.read(buf.data(), len) != (int)len) {
    readOk_ = false;
    return String();
  }
  return String((const char*)buf.data(), len);
}

bool BookReader::seekTo(uint32_t pos) {
  if (pos > fileSize_) {
    readOk_ = false;
    return false;
  }
  return file_.seek(pos, SeekSet);
}

uint16_t BookReader::peekU16At(uint32_t filePos) {
  uint32_t saved = file_.position();
  if (!seekTo(filePos)) return 0;
  uint16_t v = readU16();
  file_.seek(saved, SeekSet);
  return v;
}

bool BookReader::open(fs::FS& fs, const char* path) {
  close();
  file_ = fs.open(path, "r");
  if (!file_) return false;
  fileSize_ = file_.size();
  readOk_ = true;

  char magic[4] = {0, 0, 0, 0};
  if (file_.read((uint8_t*)magic, 4) != 4 || memcmp(magic, "CEBK", 4) != 0) {
    close();
    return false;
  }
  uint8_t version = readU8();
  if (version != 1) {
    close();
    return false;
  }

  title_ = readLenPrefixedString();
  author_ = readLenPrefixedString();

  uint16_t chapterCount = readU16();
  // A chapter table entry is at least 8 bytes on disk (1B empty title + 2×u32 offsets +
  // 3×u16 counts is more, but 8 is a safe floor), so a chapterCount that couldn't
  // possibly fit in the remaining file means the header is corrupt — bail before
  // reserving memory for it.
  if (!readOk_ || (uint32_t)chapterCount * 8u > fileSize_) {
    close();
    return false;
  }
  chapters_.clear();
  chapters_.reserve(chapterCount);

  for (uint16_t i = 0; i < chapterCount; i++) {
    ChapterIndex ch;
    ch.title = readLenPrefixedString();
    ch.textOffset = readU32();
    ch.textLength = readU32();

    ch.lineCount = readU16();
    ch.lineTableFileOffset = file_.position();
    if (!seekTo(ch.lineTableFileOffset + (uint32_t)ch.lineCount * 6)) break;

    ch.pageCount = readU16();
    ch.pageTableFileOffset = file_.position();
    if (!seekTo(ch.pageTableFileOffset + (uint32_t)ch.pageCount * 2)) break;

    ch.runCount = readU16();
    ch.runTableFileOffset = file_.position();
    if (!seekTo(ch.runTableFileOffset + (uint32_t)ch.runCount * 7)) break;

    if (!readOk_) break;
    chapters_.push_back(ch);
  }

  if (!readOk_ || chapters_.size() != chapterCount) {
    close();
    return false;
  }

  textBlobLength_ = readU32();
  textBlobFileOffset_ = file_.position();
  if (!readOk_ || textBlobFileOffset_ + textBlobLength_ > fileSize_) {
    close();
    return false;
  }
  return true;
}

void BookReader::close() {
  if (file_) file_.close();
  chapters_.clear();
  title_ = "";
  author_ = "";
  textBlobFileOffset_ = 0;
  textBlobLength_ = 0;
  fileSize_ = 0;
  readOk_ = true;
}

std::vector<Line> BookReader::getPageLines(size_t chapterIdx, uint16_t pageIdx) {
  // A short read anywhere latches readOk_ false; without resetting it here, one transient
  // failure (or one out-of-range page/chapter request) would silently blank every page
  // this BookReader ever renders again, not just the request that hit it.
  readOk_ = true;
  std::vector<Line> lines;
  if (chapterIdx >= chapters_.size()) return lines;
  const ChapterIndex& ch = chapters_[chapterIdx];
  if (pageIdx >= ch.pageCount) return lines;

  if (!seekTo(ch.pageTableFileOffset + (uint32_t)pageIdx * 2)) return lines;
  uint16_t startLine = readU16();
  uint16_t endLine = (pageIdx + 1 < ch.pageCount)
                          ? peekU16At(ch.pageTableFileOffset + (uint32_t)(pageIdx + 1) * 2)
                          : ch.lineCount;

  // Corrupt page table: a descending or out-of-range start/end used to underflow the
  // reserve() below into a multi-gigabyte allocation.
  if (startLine > endLine || endLine > ch.lineCount) return lines;

  lines.reserve((size_t)(endLine - startLine));
  if (!seekTo(ch.lineTableFileOffset + (uint32_t)startLine * 6)) return lines;
  for (uint16_t i = startLine; i < endLine; i++) {
    Line l;
    l.offset = readU32();
    l.length = readU16();
    if (!readOk_) break;
    lines.push_back(l);
  }
  return lines;
}

std::vector<StyleRun> BookReader::getChapterRuns(size_t chapterIdx) {
  readOk_ = true;  // see getPageLines() — don't let a stale failure carry over
  std::vector<StyleRun> runs;
  if (chapterIdx >= chapters_.size()) return runs;
  const ChapterIndex& ch = chapters_[chapterIdx];

  if (!seekTo(ch.runTableFileOffset)) return runs;
  runs.reserve(ch.runCount);
  for (uint16_t i = 0; i < ch.runCount; i++) {
    StyleRun r;
    r.offset = readU32();
    r.length = readU16();
    r.flags = readU8();
    if (!readOk_) break;
    runs.push_back(r);
  }
  return runs;
}

size_t BookReader::readTextInto(uint32_t offset, uint16_t length, uint8_t* out, size_t outCap) {
  readOk_ = true;  // see getPageLines() — don't let a stale failure carry over
  if (length == 0 || out == nullptr || outCap == 0) return 0;
  // Offsets are absolute into the text blob; anything past its end is corrupt data, and
  // reading it would render whatever bytes happened to follow in the file.
  if (offset >= textBlobLength_) return 0;
  uint32_t avail = textBlobLength_ - offset;
  size_t want = std::min((size_t)length, (size_t)avail);
  want = std::min(want, outCap);
  if (!seekTo(textBlobFileOffset_ + offset)) return 0;
  int got = file_.read(out, want);
  return got > 0 ? (size_t)got : 0;
}

String BookReader::readText(uint32_t offset, uint16_t length) {
  if (length == 0) return String();
  std::vector<uint8_t> buf(length);
  size_t got = readTextInto(offset, length, buf.data(), buf.size());
  if (got == 0) return String();
  return String((const char*)buf.data(), got);
}

namespace {

void wrU8(File& f, uint8_t v) { f.write(&v, 1); }

void wrU16(File& f, uint16_t v) {
  uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
  f.write(b, 2);
}

void wrU32(File& f, uint32_t v) {
  uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
  f.write(b, 4);
}

void wrPrefixed(File& f, const String& s) {
  const uint8_t* p = (const uint8_t*)s.c_str();
  size_t n = s.length();
  if (n > 255) {
    n = 255;
    while (n > 0 && (p[n] & 0xc0) == 0x80) n--;
  }
  wrU8(f, (uint8_t)n);
  if (n) f.write(p, n);
}

size_t utf8Advance(const uint8_t* s, size_t i, size_t end) {
  if (i >= end) return 0;
  uint8_t c = s[i];
  size_t n = 1;
  if ((c & 0x80) == 0) n = 1;
  else if ((c & 0xE0) == 0xC0) n = 2;
  else if ((c & 0xF0) == 0xE0) n = 3;
  else if ((c & 0xF8) == 0xF0) n = 4;
  if (i + n > end) return end - i;
  return n;
}

}  // namespace

bool BookReader::writePlain(fs::FS& fs, const char* path, const String& title, const String& author,
                            const String& body) {
  String text = body;
  text.replace("\r\n", "\n");
  text.replace("\r", "\n");
  if (text.length() > 24000) text = text.substring(0, 24000);

  const int maxW = PAGE_WIDTH_PX - 2 * MARGIN_PX;
  const int maxH = PAGE_HEIGHT_PX - 2 * MARGIN_PX;
  const uint8_t* s = (const uint8_t*)text.c_str();
  const size_t n = text.length();
  std::vector<Line> lines;
  size_t i = 0;
  while (i <= n) {
    size_t pStart = i;
    while (i < n && s[i] != '\n') i++;
    size_t pEnd = i;
    if (pEnd == pStart) {
      lines.push_back({(uint32_t)pStart, 0});
    } else {
      size_t lineStart = pStart;
      int lastBreak = -1;
      int width = 0;
      size_t j = pStart;
      while (j < pEnd) {
        size_t adv = utf8Advance(s, j, pEnd);
        int w = (s[j] == ' ') ? 3 : 6;
        if (s[j] == ' ') lastBreak = (int)j;
        if (width + w > maxW && j > lineStart) {
          size_t breakAt = (lastBreak >= (int)lineStart) ? (size_t)lastBreak : j;
          uint16_t len = (uint16_t)std::min<size_t>(breakAt - lineStart, 65535);
          lines.push_back({(uint32_t)lineStart, len});
          lineStart = (s[breakAt] == ' ') ? breakAt + 1 : breakAt;
          j = lineStart;
          width = 0;
          lastBreak = -1;
          continue;
        }
        width += w;
        j += adv;
      }
      if (lineStart < pEnd) {
        uint16_t len = (uint16_t)std::min<size_t>(pEnd - lineStart, 65535);
        lines.push_back({(uint32_t)lineStart, len});
      }
    }
    if (i < n && s[i] == '\n') {
      i++;
      continue;
    }
    break;
  }
  if (lines.empty()) lines.push_back({0, 0});
  if (lines.size() > 65535) return false;

  std::vector<uint16_t> pageStarts;
  pageStarts.push_back(0);
  int h = 0;
  for (size_t idx = 0; idx < lines.size(); idx++) {
    if (h + LINE_HEIGHT_PX > maxH && idx > pageStarts.back()) {
      pageStarts.push_back((uint16_t)idx);
      h = 0;
    }
    h += LINE_HEIGHT_PX;
  }
  if (pageStarts.size() > 65535) return false;

  File f = fs.open(path, "w");
  if (!f) return false;
  f.write((const uint8_t*)"CEBK", 4);
  wrU8(f, 1);
  wrPrefixed(f, title.length() ? title : String("Note"));
  wrPrefixed(f, author);
  wrU16(f, 1);
  wrPrefixed(f, title.length() ? title : String("Note"));
  wrU32(f, 0);
  wrU32(f, (uint32_t)text.length());
  wrU16(f, (uint16_t)lines.size());
  for (const Line& line : lines) {
    wrU32(f, line.offset);
    wrU16(f, line.length);
  }
  wrU16(f, (uint16_t)pageStarts.size());
  for (uint16_t p : pageStarts) wrU16(f, p);
  wrU16(f, 0);
  wrU32(f, (uint32_t)text.length());
  if (text.length()) f.write((const uint8_t*)text.c_str(), text.length());
  f.close();
  return true;
}
