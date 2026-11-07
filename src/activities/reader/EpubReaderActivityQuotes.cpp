#include <Epub/Page.h>
#include <Epub/blocks/TextBlock.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_system.h>

#include <algorithm>

#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "QuoteSelectActivity.h"
#include "QuoteUnderline.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/QuoteText.h"

namespace {
// Quote underlines. Positions only: 128 anchors is 1.5KB resident, and a book with
// more saved quotes than that simply stops underlining the extras.
constexpr size_t MAX_QUOTE_ANCHORS = 128;
// Heap that must remain free beyond the sidecar itself before it is read at all.
constexpr size_t QUOTE_INDEX_HEAP_HEADROOM = 8 * 1024;
// Ceiling on the word list flattened per page while looking for a quote. A full
// page of small type is well under this; the cap only bounds the allocation.
constexpr size_t MAX_PAGE_TOKENS = 512;
}  // namespace

// Index the saved quotes: where each one sits, not what it says. Called on open
// and after anything rewrites the sidecar (a new grab, a delete in the viewer).
// The 24KB read is bounded by the writer's own cap and refused when the heap
// cannot spare it; on any refusal the book simply shows no underlines.
void EpubReaderActivity::loadQuoteAnchors() {
  quoteAnchors.clear();
  underlineMemoSpine = -1;
  underlineMemoPage = -1;
  underlineMemo.clear();
  if (!epub) return;

  const std::string path = quote_text::quotesFilePathFor(epub->getPath());
  if (!Storage.exists(path.c_str())) return;  // the common case: nothing to do

  HalFile file;
  if (!Storage.openFileForRead("ERS", path, file)) return;
  const size_t fileSize = file.size();
  if (fileSize == 0 || fileSize > quote_text::MAX_QUOTES_FILE_BYTES) return;
  if (ESP.getMaxAllocHeap() < fileSize + QUOTE_INDEX_HEAP_HEADROOM) {
    LOG_ERR("ERS", "Low heap for %u byte quotes file, skipping underlines", static_cast<unsigned>(fileSize));
    return;
  }
  std::string buf;
  buf.assign(fileSize, '\0');
  if (file.read(&buf[0], fileSize) != static_cast<int>(fileSize)) return;

  // Same record grammar the viewer parses: "[chapter @anchor]\nquote\n---\n\n".
  // Records with no anchor are skipped here — they stay bare in the text.
  quoteAnchors.reserve(MAX_QUOTE_ANCHORS);
  size_t pos = 0;
  quote_text::QuoteRecord record;
  while (quoteAnchors.size() < MAX_QUOTE_ANCHORS && quote_text::nextQuoteRecord(buf, pos, record)) {
    quote_text::QuoteAnchor anchor;
    if (record.hasHeader) {
      std::string chapter, token;
      quote_text::splitChapterAnchor(buf.substr(record.headerStart, record.headerLen), chapter, token);
      if (!token.empty()) quote_text::parseAnchorToken(token, anchor);
    }
    const size_t textLength = record.textEnd - record.textStart;
    if (anchor.valid && textLength > 0 && textLength <= quote_underline::MAX_MATCH_BYTES) {
      QuoteAnchorRef ref;
      ref.textOffset = static_cast<uint32_t>(record.textStart);
      ref.textLength = static_cast<uint16_t>(textLength);
      ref.spine = anchor.spine;
      ref.paragraph = anchor.paragraph;
      ref.wordHint = anchor.wordHint;
      quoteAnchors.push_back(ref);
    }
  }
  LOG_DBG("ERS", "Quote anchors: %u", static_cast<unsigned>(quoteAnchors.size()));
}

// Draw the thin line under every saved quote visible on this page. Nothing is
// stored about where a quote sits on screen — the words are found again by their
// own text, so the line follows the passage through any font, margin or
// orientation change, and a passage that is not really here draws nothing.
void EpubReaderActivity::drawQuoteUnderlines(const Page& page, const int marginLeft, const int marginTop,
                                             const int fontId) {
  if (quoteAnchors.empty() || !section) return;

  const int lineHeight = renderer.getLineHeight(fontId);
  const int thickness = quote_underline::underlineThickness(lineHeight);
  const auto drawSegments = [&]() {
    for (const auto& seg : underlineMemo) {
      renderer.drawLine(seg.x1, seg.y, seg.x2, seg.y, thickness, true);
    }
  };

  // Same page as last render: the words have not moved, so replay the result.
  if (underlineMemoSpine == currentSpineIndex && underlineMemoPage == section->currentPage) {
    drawSegments();
    return;
  }
  underlineMemo.clear();
  underlineMemoSpine = currentSpineIndex;
  underlineMemoPage = section->currentPage;

  // Which paragraphs this page covers. A page that opens mid-paragraph carries no
  // ordinal until the next paragraph begins, so the one before that first ordinal
  // is the paragraph it opened in. A page wholly inside one paragraph has none at
  // all, and then the range check is skipped for every quote.
  uint16_t firstOrdinal = 0;
  uint16_t lastOrdinal = 0;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const uint16_t ord = static_cast<const PageLine&>(*el).getParagraphOrdinal();
    if (ord == 0) continue;
    if (firstOrdinal == 0) firstOrdinal = static_cast<uint16_t>(ord - 1);
    lastOrdinal = ord;
  }

  size_t candidates[quote_underline::MAX_QUOTES_PER_PAGE];
  size_t candidateCount = 0;
  for (size_t i = 0; i < quoteAnchors.size() && candidateCount < quote_underline::MAX_QUOTES_PER_PAGE; i++) {
    const auto& ref = quoteAnchors[i];
    if (static_cast<int>(ref.spine) != currentSpineIndex) continue;
    const bool ordinalKnown = ref.paragraph != 0 && firstOrdinal != 0;
    if (ordinalKnown) {
      // A quote starting after this page cannot show here at all. One starting before
      // it may still have its tail here, so look back a bounded number of paragraphs.
      if (ref.paragraph > lastOrdinal) continue;
      if (ref.paragraph + quote_underline::MAX_SPAN_PARAGRAPHS < firstOrdinal) continue;
    }
    candidates[candidateCount++] = i;
  }
  if (candidateCount == 0) return;  // nothing to look for: no tokens built, no SD read

  // Flatten the page's words: the text pointers the matcher walks, and the geometry
  // the line is drawn from. Pointers go straight into the TextBlock arena, which owns
  // NUL-terminated text and outlives this call, so no strings are copied.
  struct TokenBox {
    int16_t x;
    int16_t y;  // line top, ruby gap included: every word of a row shares it
    uint16_t word;
    const TextBlock* block;
  };
  // Reserve the page's real count: a box is 12 bytes, and 512 of them would all be taken.
  size_t pageTokens = 0;
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& block = static_cast<const PageLine&>(*el).getBlock();
    if (block && block->valid()) pageTokens += block->wordCount();
  }
  pageTokens = std::min(pageTokens, MAX_PAGE_TOKENS);
  std::vector<const char*> texts;
  std::vector<TokenBox> boxes;
  texts.reserve(pageTokens);
  boxes.reserve(pageTokens);
  for (const auto& el : page.elements) {
    if (el->getTag() != TAG_PageLine) continue;
    const auto& line = static_cast<const PageLine&>(*el);
    const auto& block = line.getBlock();
    if (!block || !block->valid()) continue;
    const int top = line.yPos + marginTop + block->getRubyShift(block->lineAscent(renderer, fontId));
    for (uint16_t i = 0; i < block->wordCount() && texts.size() < MAX_PAGE_TOKENS; i++) {
      texts.push_back(block->wordText(i));
      boxes.push_back({static_cast<int16_t>(line.xPos + block->wordXpos(i) + marginLeft), static_cast<int16_t>(top), i,
                       block.get()});
    }
  }
  if (texts.empty()) return;

  auto scratch = makeUniqueNoThrow<char[]>(quote_underline::MAX_MATCH_BYTES + 1);
  if (!scratch) {
    LOG_ERR("ERS", "OOM: quote underline scratch");
    return;
  }
  const std::string path = quote_text::quotesFilePathFor(epub->getPath());
  HalFile file;
  if (!Storage.openFileForRead("ERS", path, file)) return;

  for (size_t c = 0; c < candidateCount; c++) {
    const auto& ref = quoteAnchors[candidates[c]];
    if (!file.seek(ref.textOffset)) continue;
    const int read = file.read(scratch.get(), ref.textLength);
    if (read != static_cast<int>(ref.textLength)) continue;
    scratch[ref.textLength] = '\0';

    size_t first = 0, last = 0;
    if (!quote_underline::findQuoteRun(texts.data(), texts.size(), scratch.get(), ref.wordHint, first, last)) {
      // Not starting here. It may still be a quote grabbed on an earlier page whose
      // rest runs into this one, which always resumes at the page's first word.
      if (!quote_underline::findQuoteContinuation(texts.data(), texts.size(), scratch.get(), last)) continue;
      first = 0;
    }

    // One unbroken stroke per screen row: words that share a y are covered by a
    // single line from the first word's left edge to the last word's right edge,
    // so the spaces between them are underlined too.
    size_t i = first;
    while (i <= last && underlineMemo.size() < quote_underline::MAX_SEGMENTS_PER_PAGE) {
      const int16_t rowY = boxes[i].y;
      size_t rowEnd = i;
      while (rowEnd + 1 <= last && boxes[rowEnd + 1].y == rowY) rowEnd++;
      const TextBlock& rowBlock = *boxes[i].block;
      const int x2 = boxes[rowEnd].x + boxes[rowEnd].block->wordWidth(renderer, fontId, boxes[rowEnd].word);
      const int rowHeight =
          renderer.getLineHeight(TextBlock::renderFontId(renderer, rowBlock.blockFontId(fontId), fontId)) +
          rowBlock.extraAscent(renderer, fontId);
      underlineMemo.push_back({boxes[i].x, static_cast<int16_t>(x2),
                               static_cast<int16_t>(quote_underline::underlineY(
                                   rowY, rowBlock.lineAscent(renderer, fontId), rowHeight, thickness))});
      i = rowEnd + 1;
    }
  }
  drawSegments();
}
