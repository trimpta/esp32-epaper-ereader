#include "book_format.h"

uint8_t BookReader::readU8() {
  uint8_t v = 0;
  file_.read(&v, 1);
  return v;
}

uint16_t BookReader::readU16() {
  uint8_t b[2];
  file_.read(b, 2);
  return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

uint32_t BookReader::readU32() {
  uint8_t b[4];
  file_.read(b, 4);
  return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

String BookReader::readLenPrefixedString() {
  uint8_t len = readU8();
  if (len == 0) return String();
  std::vector<uint8_t> buf(len);
  file_.read(buf.data(), len);
  return String((const char*)buf.data(), len);
}

uint16_t BookReader::peekU16At(uint32_t filePos) {
  uint32_t saved = file_.position();
  file_.seek(filePos, SeekSet);
  uint16_t v = readU16();
  file_.seek(saved, SeekSet);
  return v;
}

bool BookReader::open(fs::FS& fs, const char* path) {
  close();
  file_ = fs.open(path, "r");
  if (!file_) return false;

  char magic[4];
  file_.read((uint8_t*)magic, 4);
  if (memcmp(magic, "CEBK", 4) != 0) {
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
  chapters_.clear();
  chapters_.reserve(chapterCount);

  for (uint16_t i = 0; i < chapterCount; i++) {
    ChapterIndex ch;
    ch.title = readLenPrefixedString();
    ch.textOffset = readU32();
    ch.textLength = readU32();

    ch.lineCount = readU16();
    ch.lineTableFileOffset = file_.position();
    file_.seek((uint32_t)ch.lineCount * 6, SeekCur);

    ch.pageCount = readU16();
    ch.pageTableFileOffset = file_.position();
    file_.seek((uint32_t)ch.pageCount * 2, SeekCur);

    ch.runCount = readU16();
    ch.runTableFileOffset = file_.position();
    file_.seek((uint32_t)ch.runCount * 7, SeekCur);

    chapters_.push_back(ch);
  }

  readU32();  // textBlobLength — not needed, blob starts right here and offsets are absolute
  textBlobFileOffset_ = file_.position();
  return true;
}

void BookReader::close() {
  if (file_) file_.close();
  chapters_.clear();
  title_ = "";
  author_ = "";
}

std::vector<Line> BookReader::getPageLines(size_t chapterIdx, uint16_t pageIdx) {
  std::vector<Line> lines;
  if (chapterIdx >= chapters_.size()) return lines;
  const ChapterIndex& ch = chapters_[chapterIdx];
  if (pageIdx >= ch.pageCount) return lines;

  file_.seek(ch.pageTableFileOffset + (uint32_t)pageIdx * 2, SeekSet);
  uint16_t startLine = readU16();
  uint16_t endLine = (pageIdx + 1 < ch.pageCount)
                          ? peekU16At(ch.pageTableFileOffset + (uint32_t)(pageIdx + 1) * 2)
                          : ch.lineCount;

  lines.reserve(endLine - startLine);
  file_.seek(ch.lineTableFileOffset + (uint32_t)startLine * 6, SeekSet);
  for (uint16_t i = startLine; i < endLine; i++) {
    Line l;
    l.offset = readU32();
    l.length = readU16();
    lines.push_back(l);
  }
  return lines;
}

std::vector<StyleRun> BookReader::getChapterRuns(size_t chapterIdx) {
  std::vector<StyleRun> runs;
  if (chapterIdx >= chapters_.size()) return runs;
  const ChapterIndex& ch = chapters_[chapterIdx];

  file_.seek(ch.runTableFileOffset, SeekSet);
  runs.reserve(ch.runCount);
  for (uint16_t i = 0; i < ch.runCount; i++) {
    StyleRun r;
    r.offset = readU32();
    r.length = readU16();
    r.flags = readU8();
    runs.push_back(r);
  }
  return runs;
}

String BookReader::readText(uint32_t offset, uint16_t length) {
  if (length == 0) return String();
  file_.seek(textBlobFileOffset_ + offset, SeekSet);
  std::vector<uint8_t> buf(length);
  file_.read(buf.data(), length);
  return String((const char*)buf.data(), length);
}
