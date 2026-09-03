// EPUB -> .cebk converter. Runs entirely in the browser (no build step).
// Depends on a global `JSZip` (loaded via <script> before this file — see index.html).
// Format spec: ../docs/FORMAT.md — keep in sync with firmware/src/book_format.h.

const STYLE = { BOLD: 1, ITALIC: 2, H1: 4, H2: 8 };

// Landscape (rotated) layout — see firmware/src/config.h for why. The panel is
// natively 122x250 portrait; the firmware transposes it via setRotation(1).
const LAYOUT = {
  PAGE_WIDTH_PX: 250,
  PAGE_HEIGHT_PX: 122,
  MARGIN_PX: 4,
  LINE_HEIGHT_PX: 11,
  HEADING_LINE_HEIGHT_PX: 14,
};

const BLOCK_TAGS = new Set([
  'p', 'div', 'li', 'h1', 'h2', 'h3', 'h4', 'h5', 'h6', 'br', 'blockquote',
]);

// ---------------------------------------------------------------------------
// EPUB parsing
// ---------------------------------------------------------------------------

async function loadEpub(arrayBuffer) {
  const zip = await JSZip.loadAsync(arrayBuffer);

  const containerFile = zip.file('META-INF/container.xml');
  if (!containerFile) throw new Error('Not a valid EPUB: missing META-INF/container.xml');
  const containerXml = await containerFile.async('string');
  const containerDoc = new DOMParser().parseFromString(containerXml, 'application/xml');
  const opfPath = containerDoc.querySelector('rootfile').getAttribute('full-path');
  const opfDir = opfPath.includes('/') ? opfPath.slice(0, opfPath.lastIndexOf('/') + 1) : '';

  const opfFile = zip.file(opfPath);
  if (!opfFile) throw new Error(`OPF not found at ${opfPath}`);
  const opfText = await opfFile.async('string');
  const opfDoc = new DOMParser().parseFromString(opfText, 'application/xml');

  // Not scoped to a `metadata >` parent: real-world EPUBs vary on whether that element
  // is `<metadata>` or `<opf:metadata>` (confirmed against a real Adelaide-eBooks EPUB),
  // and an unscoped search is robust to either.
  const title = firstText(opfDoc, ['dc\\:title', 'title']) || 'Untitled';
  const author = firstText(opfDoc, ['dc\\:creator', 'creator']) || '';

  const manifest = {};
  opfDoc.querySelectorAll('manifest > item').forEach((item) => {
    manifest[item.getAttribute('id')] = item.getAttribute('href');
  });

  const spineHrefs = [];
  opfDoc.querySelectorAll('spine > itemref').forEach((itemref) => {
    const idref = itemref.getAttribute('idref');
    if (manifest[idref]) spineHrefs.push(opfDir + manifest[idref]);
  });

  const chapters = [];
  for (const href of spineHrefs) {
    const cleanHref = decodeURIComponent(href.split('#')[0]);
    const file = zip.file(cleanHref);
    if (!file) continue;
    const xhtml = await file.async('string');
    let doc = new DOMParser().parseFromString(xhtml, 'application/xhtml+xml');
    if (doc.querySelector('parsererror')) {
      doc = new DOMParser().parseFromString(xhtml, 'text/html');
    }
    if (!doc.body) continue;

    const { text, runs } = extractChapterContent(doc.body);
    if (!text.trim()) continue;

    const chapterTitle = pickChapterTitle(doc, title, chapters.length + 1);
    chapters.push({ title: chapterTitle, text, runs });
  }

  return { title, author, chapters };
}

function firstText(doc, selectors) {
  for (const sel of selectors) {
    try {
      const el = doc.querySelector(sel);
      if (el && el.textContent.trim()) return el.textContent.trim();
    } catch {
      /* invalid selector on this doc — ignore, try next */
    }
  }
  return null;
}

// Many real EPUBs repeat a book-title masthead (<h1>Book Title</h1>) at the top of
// every chapter file, with the actual chapter heading further down as an h2/h3
// (confirmed against a real Adelaide-eBooks EPUB — "Chapter 2" was an <h3>, past an
// <h1> masthead identical on every page). Picking the first heading regardless of
// level, but skipping any that just repeat the book title, avoids a library full of
// chapters that all display the same name.
function pickChapterTitle(doc, bookTitle, fallbackIndex) {
  if (!doc.body) return `Chapter ${fallbackIndex}`;

  // h2-h6 first: in the real EPUB this was checked against, the masthead was an <h1>
  // ("Animal Farm, by George Orwell", identical on every page) and the actual per-
  // chapter heading was an <h3> ("Chapter 2") — comparing heading text against the
  // book title doesn't catch this, since the masthead isn't a verbatim title match,
  // just a repeated non-answer. Preferring the deeper heading level does.
  for (const h of doc.body.querySelectorAll('h2, h3, h4, h5, h6')) {
    // Collapses internal whitespace too, not just leading/trailing — a heading split
    // across nested elements (e.g. "Chapter II" and its name in separate <span>s on
    // separate lines) otherwise keeps a literal newline in the extracted title.
    const t = h.textContent.replace(/\s+/g, ' ').trim();
    if (t) return t.slice(0, 255);
  }

  // Only h1s (or none deeper) — still worth using, unless it's just the book title
  // again (common on a title/cover page).
  const normalizedBookTitle = (bookTitle || '').trim().toLowerCase();
  for (const h of doc.body.querySelectorAll('h1')) {
    // Collapses internal whitespace too, not just leading/trailing — a heading split
    // across nested elements (e.g. "Chapter II" and its name in separate <span>s on
    // separate lines) otherwise keeps a literal newline in the extracted title.
    const t = h.textContent.replace(/\s+/g, ' ').trim();
    if (t && t.toLowerCase() !== normalizedBookTitle) return t.slice(0, 255);
  }
  return `Chapter ${fallbackIndex}`;
}

// Walks a chapter's <body>, producing:
//   text: plain string with block elements separated by '\n'
//   runs: [{ offset, length, flags }] in codepoint units, relative to this chapter's text
//
// Only bold/italic/h1/h2(-6) are tracked; everything else (CSS, images, links,
// tables, footnotes) is dropped on the floor by design — see docs/ARCHITECTURE.md.
function extractChapterContent(bodyEl) {
  let text = '';
  let cpLength = 0;
  const runs = [];
  const styleStack = [];

  const currentFlags = () => styleStack.reduce((a, b) => a | b, 0);

  function appendText(raw) {
    const collapsed = raw.replace(/\s+/g, ' ');
    if (!collapsed) return;
    // avoid stacking multiple spaces across adjacent inline elements
    if (collapsed === ' ' && (text.endsWith(' ') || text.endsWith('\n') || text === '')) return;
    text += collapsed;
    cpLength += Array.from(collapsed).length;
  }

  function walk(node) {
    if (node.nodeType === Node.TEXT_NODE) {
      const start = cpLength;
      appendText(node.textContent);
      const length = cpLength - start;
      if (length > 0) {
        const flags = currentFlags();
        if (flags) runs.push({ offset: start, length, flags });
      }
      return;
    }
    if (node.nodeType !== Node.ELEMENT_NODE) return;

    const tag = node.tagName.toLowerCase();
    if (tag === 'script' || tag === 'style' || tag === 'head') return;

    let flag = 0;
    if (tag === 'b' || tag === 'strong') flag = STYLE.BOLD;
    else if (tag === 'i' || tag === 'em') flag = STYLE.ITALIC;
    else if (tag === 'h1') flag = STYLE.H1;
    else if (/^h[2-6]$/.test(tag)) flag = STYLE.H2;

    if (flag) styleStack.push(flag);
    for (const child of node.childNodes) walk(child);
    if (flag) styleStack.pop();

    if (BLOCK_TAGS.has(tag) && !text.endsWith('\n')) {
      text += '\n';
      cpLength += 1;
    }
  }

  walk(bodyEl);
  return { text: text.trim(), runs };
}

// ---------------------------------------------------------------------------
// Line wrapping + pagination (codepoint-index space throughout)
// ---------------------------------------------------------------------------

function styleAt(cpOffset, runs) {
  let flags = 0;
  for (const r of runs) {
    if (cpOffset >= r.offset && cpOffset < r.offset + r.length) flags |= r.flags;
  }
  return flags;
}

function metricsKeyForFlags(flags) {
  if (flags & STYLE.H1) return 'heading1';
  if (flags & STYLE.H2) return 'heading2';
  if (flags & STYLE.BOLD) return 'bold';
  if (flags & STYLE.ITALIC) return 'italic';
  return 'regular';
}

function charWidth(fontMetrics, flags, ch) {
  const key = metricsKeyForFlags(flags);
  const face = fontMetrics[key] || fontMetrics.regular;
  return face.widths[ch] ?? face.default;
}

// Greedy word-wrap over a chapter's codepoint array. Returns lines as
// { offset, length, heading } with offsets relative to the chapter start.
function wrapChapter(chars, runs, fontMetrics, layout) {
  const maxWidth = layout.PAGE_WIDTH_PX - 2 * layout.MARGIN_PX;
  const lines = [];

  // split into paragraphs on '\n', keeping empty paragraphs (blank lines) for spacing
  const paraBounds = [];
  let paraStart = 0;
  for (let i = 0; i <= chars.length; i++) {
    if (i === chars.length || chars[i] === '\n') {
      paraBounds.push([paraStart, i]);
      paraStart = i + 1;
    }
  }

  for (const [pStart, pEnd] of paraBounds) {
    if (pEnd === pStart) {
      lines.push({ offset: pStart, length: 0, heading: false });
      continue;
    }
    let lineStart = pStart;
    let lastBreak = -1;
    let width = 0;
    let i = pStart;
    while (i < pEnd) {
      const ch = chars[i];
      const flags = styleAt(i, runs);
      const w = charWidth(fontMetrics, flags, ch);
      if (ch === ' ') lastBreak = i;

      if (width + w > maxWidth && i > lineStart) {
        const breakAt = lastBreak >= lineStart ? lastBreak : i;
        lines.push({
          offset: lineStart,
          length: breakAt - lineStart,
          heading: isHeading(lineStart, runs),
        });
        lineStart = chars[breakAt] === ' ' ? breakAt + 1 : breakAt;
        i = lineStart;
        width = 0;
        lastBreak = -1;
        continue;
      }
      width += w;
      i++;
    }
    if (lineStart < pEnd) {
      lines.push({
        offset: lineStart,
        length: pEnd - lineStart,
        heading: isHeading(lineStart, runs),
      });
    }
  }
  return lines;
}

function isHeading(cpOffset, runs) {
  const f = styleAt(cpOffset, runs);
  return !!(f & (STYLE.H1 | STYLE.H2));
}

function paginate(lines, layout) {
  const maxHeight = layout.PAGE_HEIGHT_PX - 2 * layout.MARGIN_PX;
  const pageStarts = [0];
  let h = 0;
  for (let idx = 0; idx < lines.length; idx++) {
    const lh = lines[idx].heading ? layout.HEADING_LINE_HEIGHT_PX : layout.LINE_HEIGHT_PX;
    if (h + lh > maxHeight && idx > pageStarts[pageStarts.length - 1]) {
      pageStarts.push(idx);
      h = 0;
    }
    h += lh;
  }
  return pageStarts;
}

// ---------------------------------------------------------------------------
// Binary encoding — see docs/FORMAT.md
// ---------------------------------------------------------------------------

class BinaryWriter {
  constructor() {
    this.bytes = [];
  }
  u8(v) {
    this.bytes.push(v & 0xff);
  }
  u16(v) {
    this.u8(v);
    this.u8(v >> 8);
  }
  u32(v) {
    this.u8(v);
    this.u8(v >> 8);
    this.u8(v >> 16);
    this.u8(v >> 24);
  }
  rawBytes(u8arr) {
    for (const b of u8arr) this.bytes.push(b);
  }
  // The length prefix is one byte, so this truncates on a UTF-8 *byte* boundary rather
  // than throwing. Callers used to slice by character count, which isn't the same limit at
  // all: a 200-character title in any non-Latin script is well over 255 bytes, and threw.
  utf8String(str) {
    let enc = new TextEncoder().encode(str);
    if (enc.length > 255) {
      let end = 255;
      // Don't cut a multi-byte character in half — back up off any continuation byte.
      while (end > 0 && (enc[end] & 0xc0) === 0x80) end--;
      enc = enc.slice(0, end);
    }
    this.u8(enc.length);
    this.rawBytes(enc);
  }
  toUint8Array() {
    return Uint8Array.from(this.bytes);
  }
}

// Builds a codepoint-index -> byte-offset table for a string, so line/run
// offsets computed in codepoint space can be translated to the UTF-8 byte
// offsets the device actually reads.
function buildByteOffsetTable(str) {
  const codepoints = Array.from(str);
  const encoder = new TextEncoder();
  const table = new Array(codepoints.length + 1);
  let byteOffset = 0;
  for (let i = 0; i < codepoints.length; i++) {
    table[i] = byteOffset;
    byteOffset += encoder.encode(codepoints[i]).length;
  }
  table[codepoints.length] = byteOffset;
  return table;
}

async function convertEpub(arrayBuffer, fontMetrics, layout = LAYOUT, onProgress = () => {}) {
  onProgress('Unzipping and parsing EPUB…');
  const book = await loadEpub(arrayBuffer);
  if (book.chapters.length === 0) throw new Error('No readable chapters found in this EPUB.');

  onProgress('Wrapping and paginating text…');
  // Process each chapter in codepoint-local space, then compute a chapter-start
  // codepoint offset once we know how chapters concatenate.
  let fullText = '';
  const chapterResults = [];
  for (const chapter of book.chapters) {
    const chars = Array.from(chapter.text);
    const lines = wrapChapter(chars, chapter.runs, fontMetrics, layout);
    const pageStarts = paginate(lines, layout);
    const chapterCpOffset = Array.from(fullText).length;
    fullText += chapter.text;
    chapterResults.push({
      title: chapter.title,
      cpOffset: chapterCpOffset,
      cpLength: chars.length,
      lines,
      pageStarts,
      runs: chapter.runs,
    });
  }

  onProgress('Encoding binary…');
  const byteOffsetTable = buildByteOffsetTable(fullText);
  const toByte = (cp) => byteOffsetTable[cp];

  const w = new BinaryWriter();
  w.rawBytes(new TextEncoder().encode('CEBK'));
  w.u8(1); // version
  w.utf8String(book.title);   // truncated to 255 *bytes* by the writer, not 255 chars
  w.utf8String(book.author);
  w.u16(chapterResults.length);

  for (const ch of chapterResults) {
    w.utf8String(ch.title);
    const textOffset = toByte(ch.cpOffset);
    const textLength = toByte(ch.cpOffset + ch.cpLength) - textOffset;
    w.u32(textOffset);
    w.u32(textLength);

    w.u16(ch.lines.length);
    for (const line of ch.lines) {
      const abs = ch.cpOffset + line.offset;
      const lo = toByte(abs);
      const llen = toByte(abs + line.length) - lo;
      w.u32(lo);
      w.u16(llen);
    }

    w.u16(ch.pageStarts.length);
    for (const lineIdx of ch.pageStarts) w.u16(lineIdx);

    w.u16(ch.runs.length);
    for (const run of ch.runs) {
      const abs = ch.cpOffset + run.offset;
      const ro = toByte(abs);
      const rlen = toByte(abs + run.length) - ro;
      w.u32(ro);
      w.u16(rlen);
      w.u8(run.flags);
    }
  }

  const textBlobBytes = new TextEncoder().encode(fullText);
  w.u32(textBlobBytes.length);
  w.rawBytes(textBlobBytes);

  onProgress('Done.');
  return w.toUint8Array();
}

// Exposed for index.html
window.EpubConverter = { convertEpub, LAYOUT, STYLE };
