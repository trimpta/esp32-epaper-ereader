#pragma once
// Reader for the .cebk format — see docs/FORMAT.md. Reads directly from LittleFS on
// demand (seek + small reads); never loads a whole book into RAM. Keep in sync with
// converter/converter.js.

#include <Arduino.h>
#include <FS.h>
#include <vector>

enum StyleFlag : uint8_t {
  STYLE_BOLD = 1,
  STYLE_ITALIC = 2,
  STYLE_H1 = 4,
  STYLE_H2 = 8,
};

struct Line {
  uint32_t offset;
  uint16_t length;
};

struct StyleRun {
  uint32_t offset;
  uint16_t length;
  uint8_t flags;
};

struct ChapterIndex {
  String title;
  uint32_t textOffset = 0;
  uint32_t textLength = 0;

  uint32_t lineTableFileOffset = 0;
  uint16_t lineCount = 0;

  uint32_t pageTableFileOffset = 0;
  uint16_t pageCount = 0;

  uint32_t runTableFileOffset = 0;
  uint16_t runCount = 0;
};

class BookReader {
 public:
  // Reads the header + chapter table (small — a few KB at most) into RAM.
  // Everything else (line/page/run tables, text) stays on flash and is read on demand.
  bool open(fs::FS& fs, const char* path);
  void close();
  bool isOpen() const { return file_; }

  const String& title() const { return title_; }
  const String& author() const { return author_; }
  size_t chapterCount() const { return chapters_.size(); }
  const ChapterIndex& chapter(size_t idx) const { return chapters_[idx]; }

  // Lines belonging to one page, in render order. Cheap — bounded by lines-per-page.
  std::vector<Line> getPageLines(size_t chapterIdx, uint16_t pageIdx);

  // All style runs for a chapter. Cached by the caller across page turns within the
  // same chapter — don't call this per page.
  std::vector<StyleRun> getChapterRuns(size_t chapterIdx);

  // Reads [offset, offset+length) of the UTF-8 text blob.
  String readText(uint32_t offset, uint16_t length);

  // Reads [offset, offset+length) of the text blob into `out` (no String allocation).
  // Returns the number of bytes actually read. Used by the renderer to pull a whole
  // page's text in one seek+read instead of one per styled span.
  size_t readTextInto(uint32_t offset, uint16_t length, uint8_t* out, size_t outCap);

 private:
  File file_;
  String title_;
  String author_;
  std::vector<ChapterIndex> chapters_;
  uint32_t textBlobFileOffset_ = 0;
  uint32_t textBlobLength_ = 0;
  uint32_t fileSize_ = 0;
  bool readOk_ = true;  // cleared by any short read; open() treats it as "file is corrupt"

  uint8_t readU8();
  uint16_t readU16();
  uint32_t readU32();
  String readLenPrefixedString();
  uint16_t peekU16At(uint32_t filePos);
  bool seekTo(uint32_t pos);
};
