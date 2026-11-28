#include "MarkdownParser.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#ifdef ARDUINO
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

#include <algorithm>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/css/CssStyle.h"
#include "MarkdownSyntax.h"

namespace {
constexpr size_t READ_CHUNK_BYTES = 1024;
constexpr size_t LINE_CHUNK_FLUSH_BYTES = 1024;
constexpr size_t MAX_TOC_TITLE_BYTES = 256;
constexpr int16_t QUOTE_BAR_THICKNESS = 3;
constexpr int16_t QUOTE_BAR_GAP = 8;
constexpr uint32_t YIELD_LINE_INTERVAL = 64;
constexpr uint32_t YIELD_PAGE_INTERVAL = 16;

CssTextAlign resolveParagraphAlign(const uint8_t alignment) {
  if (alignment == static_cast<uint8_t>(CssTextAlign::None)) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(alignment);
}

void yieldToScheduler() {
#ifdef ARDUINO
  vTaskDelay(1);
#endif
}

}  // namespace

using namespace markdown_syntax;

MarkdownParser::MarkdownParser(const std::string& filepath, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                               const PageCompleteFn completePageFn, void* const completePageCtx,
                               const ProgressFn progressFn, void* const progressCtx)
    : filepath_(filepath),
      renderer_(renderer),
      spec_(spec),
      completePageFn_(completePageFn),
      completePageCtx_(completePageCtx),
      progressFn_(progressFn),
      progressCtx_(progressCtx) {}

MarkdownParser::~MarkdownParser() = default;

std::string MarkdownParser::stripInlineMarkdown(const std::string_view text) {
  return markdown_syntax::stripInlineMarkdown(text);
}

void MarkdownParser::appendInlineText(ParsedText& target, const std::string_view text, const uint8_t baseStyle,
                                      const bool allowSoftFlush) {
  std::string word;
  word.reserve(48);
  bool bold = inlineBold_ || (baseStyle & EpdFontFamily::BOLD) != 0;
  bool italic = inlineItalic_ || (baseStyle & EpdFontFamily::ITALIC) != 0;
  bool nextContinues = false;

  const auto currentStyle = [&]() -> EpdFontFamily::Style {
    uint8_t s = baseStyle;
    if (bold) s |= EpdFontFamily::BOLD;
    if (italic) s |= EpdFontFamily::ITALIC;
    return static_cast<EpdFontFamily::Style>(s);
  };

  const auto flushWord = [&](const bool attachNext, const uint8_t sizeSlot = 0) {
    if (!word.empty()) {
      target.addWord(word, currentStyle(), nextContinues, 0, sizeSlot);
      word.clear();
      nextContinues = attachNext;
      if (target.size() > maxBufferedWords_) maxBufferedWords_ = target.size();
      if (allowSoftFlush && target.size() >= SOFT_FLUSH_WORDS) {
        softFlushOpenBlock();
      }
    } else if (!attachNext) {
      nextContinues = false;
    }
  };

  size_t i = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (c == '\\' && i + 1 < text.size()) {
      word.push_back(text[i + 1]);
      i += 2;
      continue;
    }
    if (isAsciiSpace(c)) {
      flushWord(false);
      ++i;
      continue;
    }
    if (c == '`') {
      const size_t close = text.find('`', i + 1);
      if (close != std::string_view::npos && close > i + 1) {
        const bool hadWordBefore = !word.empty();
        flushWord(hadWordBefore);
        const uint8_t monoSlot = target.sizeSlotFor(1.0f, /*monospace=*/true);
        const std::string_view codeSpan = text.substr(i + 1, close - i - 1);
        size_t p = 0;
        while (p < codeSpan.size()) {
          while (p < codeSpan.size() && isAsciiSpace(codeSpan[p])) ++p;
          const size_t start = p;
          while (p < codeSpan.size() && !isAsciiSpace(codeSpan[p])) ++p;
          if (p > start) {
            word.assign(codeSpan.data() + start, p - start);
            flushWord(false, monoSlot);
          }
        }
        nextContinues = close + 1 < text.size() && !isAsciiSpace(text[close + 1]);
        i = close + 1;
        continue;
      }
    }
    if (c == '[') {
      std::string_view label;
      size_t endPos = 0;
      if (matchInlineLink(text, i, label, endPos)) {
        const bool hadWordBefore = !word.empty();
        flushWord(hadWordBefore);
        const size_t beforeCount = target.size();
        const bool savedBold = inlineBold_;
        const bool savedItalic = inlineItalic_;
        inlineBold_ = bold;
        inlineItalic_ = italic;
        appendInlineText(target, label, static_cast<uint8_t>(currentStyle()), allowSoftFlush);
        inlineBold_ = savedBold;
        inlineItalic_ = savedItalic;
        nextContinues = target.size() > beforeCount && endPos < text.size() && !isAsciiSpace(text[endPos]);
        i = endPos;
        continue;
      }
    }
    if (i + 2 < text.size() && text.substr(i, 3) == "***") {
      if ((bold && italic) || hasClosingDelimiter(text, i + 3, "***")) {
        flushWord(!word.empty());
        bold = !bold;
        italic = !italic;
        i += 3;
        continue;
      }
    }
    if (i + 1 < text.size() && text.substr(i, 2) == "**") {
      if (bold || hasClosingDelimiter(text, i + 2, "**")) {
        flushWord(!word.empty());
        bold = !bold;
        i += 2;
        continue;
      }
    }
    if (c == '*') {
      if (italic || hasClosingDelimiter(text, i + 1, "*")) {
        flushWord(!word.empty());
        italic = !italic;
        i += 1;
        continue;
      }
    }
    word.push_back(c);
    ++i;
  }
  flushWord(false);
  inlineBold_ = (baseStyle & EpdFontFamily::BOLD) != 0 ? false : bold;
  inlineItalic_ = (baseStyle & EpdFontFamily::ITALIC) != 0 ? false : italic;
}

void MarkdownParser::beginHeading(const uint8_t level, const std::string_view text) {
  flushOpenBlock();
  const int lineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.marginTop = static_cast<int16_t>(level == 1 ? lineH / 2 : lineH / 3);
  style.marginBottom = static_cast<int16_t>(lineH / 3);

  static constexpr float SCALES[] = {1.5f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f};
  currentBlockScale_ = SCALES[std::clamp<uint8_t>(level, 1, 6) - 1];
  currentBlockTopApplied_ = false;
  inlineBold_ = false;
  inlineItalic_ = false;

  currentBlock_ = makeUniqueNoThrow<ParsedText>(false, GUIDE_DOTS_OFF, style, 0, spec_.wordSpacing);
  if (!currentBlock_) {
    LOG_ERR("MDP", "OOM: heading block");
    return;
  }
  currentBlock_->setHeading(true);
  currentBlock_->suppressFirstLineIndent();
  blockKind_ = BlockKind::Heading;

  if (level <= 2) {
    pendingToc_ = true;
    pendingTocLevel_ = level;
    pendingTocTitle_ = stripInlineMarkdown(text);
    if (pendingTocTitle_.size() > MAX_TOC_TITLE_BYTES) pendingTocTitle_.resize(MAX_TOC_TITLE_BYTES);
  }
  appendInlineText(*currentBlock_, text, EpdFontFamily::BOLD, /*allowSoftFlush=*/true);
}

void MarkdownParser::appendCodeText(const std::string_view text) {
  if (!currentBlock_) return;
  size_t i = 0;
  while (i < text.size()) {
    while (i < text.size() && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r')) ++i;
    const size_t start = i;
    while (i < text.size() && text[i] != ' ' && text[i] != '\t' && text[i] != '\r') ++i;
    if (i > start) {
      currentBlock_->addWord(std::string(text.data() + start, i - start), EpdFontFamily::REGULAR, false, 0);
      if (currentBlock_->size() > maxBufferedWords_) maxBufferedWords_ = currentBlock_->size();
      if (currentBlock_->size() >= SOFT_FLUSH_WORDS) {
        softFlushOpenBlock();
      }
    }
  }
}

void MarkdownParser::processCodeLineStart(const std::string_view chunk, const bool isLineEnd) {
  const std::string_view line = isLineEnd ? trimRight(chunk) : chunk;
  if (isLineEnd && isFenceClosing(line, fenceChar_, fenceLen_)) {
    flushOpenBlock();
    inCodeFence_ = false;
    const int baseLineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
    if (spec_.extraParagraphSpacing) {
      currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + baseLineH / 2);
    }
    return;
  }

  flushOpenBlock();
  if (isLineEnd && trimLeft(line).empty()) {
    const int mono = renderer_.resolveMonospaceFont(spec_.fontId, 1.0f);
    const int lineH = renderer_.getLineHeight(mono != 0 ? mono : spec_.fontId, spec_.lineCompression);
    if (currentPage_ && !currentPage_->elements.empty() && currentPageNextY_ + lineH <= spec_.viewportHeight) {
      currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + lineH);
    }
    return;
  }

  // Preserve leading indentation of code lines (capped to 1/4 viewport).
  size_t indentSpaces = 0;
  for (const char c : line) {
    if (c == ' ')
      ++indentSpaces;
    else if (c == '\t')
      indentSpaces += 2;
    else
      break;
  }
  const int em = std::max(8, renderer_.getFontAscenderSize(spec_.fontId));
  const int baseIndent = std::min<int>(em / 2, spec_.viewportWidth / 12);
  const int extraIndent = std::min<int>(static_cast<int>(indentSpaces) * (em / 2), spec_.viewportWidth / 4);

  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.marginLeft = static_cast<int16_t>(baseIndent + extraIndent);

  currentBlockScale_ = 1.0f;
  currentBlockTopApplied_ = false;
  currentBlock_ = makeUniqueNoThrow<ParsedText>(false, GUIDE_DOTS_OFF, style, 0, spec_.wordSpacing);
  if (!currentBlock_) return;
  currentBlock_->suppressFirstLineIndent();
  blockKind_ = BlockKind::CodeLine;
  appendCodeText(line);
  if (isLineEnd) flushOpenBlock();
}

void MarkdownParser::processLineStart(const std::string_view rawChunk, const bool isLineEnd) {
  if (inCodeFence_) {
    processCodeLineStart(rawChunk, isLineEnd);
    return;
  }

  const std::string_view line = isLineEnd ? trimRight(rawChunk) : rawChunk;
  if (isLineEnd && trimLeft(line).empty()) {
    flushOpenBlock();
    return;
  }

  // 0. Fenced code block opening (``` or ~~~)
  char fenceChar = '`';
  if (const uint8_t fenceLen = parseFenceOpening(line, fenceChar); fenceLen >= 3) {
    flushOpenBlock();
    inCodeFence_ = true;
    fenceChar_ = fenceChar;
    fenceLen_ = fenceLen;
    return;
  }

  // 1. ATX Headings (# / ## / ### ...)
  std::string_view headingText;
  if (const uint8_t level = parseHeadingPrefix(line, headingText); level > 0) {
    beginHeading(level, headingText);
    if (isLineEnd) flushOpenBlock();
    return;
  }

  // 2. Blockquote (> text)
  const std::string_view trimmed = trimLeft(line);
  if (!trimmed.empty() && trimmed[0] == '>') {
    std::string_view quoteBody = trimmed.substr(1);
    if (!quoteBody.empty() && quoteBody.front() == ' ') quoteBody.remove_prefix(1);
    if (isLineEnd && trimLeft(quoteBody).empty()) {
      flushOpenBlock();
      return;
    }
    if (blockKind_ != BlockKind::Quote) {
      flushOpenBlock();
      const int em = std::max(8, renderer_.getFontAscenderSize(spec_.fontId));
      const int16_t quoteMargin = static_cast<int16_t>(std::min<int>(em, spec_.viewportWidth / 8));
      BlockStyle style;
      style.alignment = resolveParagraphAlign(spec_.paragraphAlignment);
      style.marginLeft = quoteMargin;
      style.paddingLeft = static_cast<int16_t>(QUOTE_BAR_THICKNESS + QUOTE_BAR_GAP);
      style.marginRight = static_cast<int16_t>(quoteMargin / 2);
      currentBlockScale_ = 1.0f;
      currentBlockTopApplied_ = false;
      currentBlock_ =
          makeUniqueNoThrow<ParsedText>(spec_.focusReadingEnabled, spec_.guideDotsMode, style, 0, spec_.wordSpacing);
      if (!currentBlock_) return;
      currentBlock_->suppressFirstLineIndent();
      inQuoteBox_ = true;
      quoteBoxX_ = quoteMargin;
      quoteBoxWidth_ = static_cast<int16_t>(std::max<int>(8, spec_.viewportWidth - quoteMargin - style.marginRight));
      quoteBoxTop_ = quoteBoxBottom_ = -1;
      blockKind_ = BlockKind::Quote;
    }
    appendInlineText(*currentBlock_, quoteBody, EpdFontFamily::REGULAR, /*allowSoftFlush=*/true);
    return;
  }

  // 3. Unordered list (- / * / +) and Ordered list (1. / 2.)
  std::string_view itemText;
  std::string orderedMarker;
  const bool isBullet = parseBulletPrefix(line, itemText);
  const bool isOrdered = !isBullet && parseOrderedPrefix(line, orderedMarker, itemText);
  if (isBullet || isOrdered) {
    flushOpenBlock();
    const int em = std::max(8, renderer_.getFontAscenderSize(spec_.fontId));
    BlockStyle style;
    style.alignment = resolveParagraphAlign(spec_.paragraphAlignment);
    style.marginLeft = static_cast<int16_t>(std::min<int>(em, spec_.viewportWidth / 8));
    currentBlockScale_ = 1.0f;
    currentBlockTopApplied_ = false;
    currentBlock_ =
        makeUniqueNoThrow<ParsedText>(spec_.focusReadingEnabled, spec_.guideDotsMode, style, 0, spec_.wordSpacing);
    if (!currentBlock_) return;
    currentBlock_->suppressFirstLineIndent();
    currentBlock_->addWord(isBullet ? "\xe2\x80\xa2" : orderedMarker, EpdFontFamily::REGULAR, false, 0);
    blockKind_ = BlockKind::ListItem;
    appendInlineText(*currentBlock_, itemText, EpdFontFamily::REGULAR, /*allowSoftFlush=*/true);
    return;
  }

  // 4. Plain paragraph line (or continuation line of open paragraph / list item)
  if (blockKind_ == BlockKind::Quote || blockKind_ == BlockKind::Heading || blockKind_ == BlockKind::CodeLine) {
    flushOpenBlock();
  }
  if (!currentBlock_) {
    BlockStyle style;
    style.alignment = resolveParagraphAlign(spec_.paragraphAlignment);
    currentBlockScale_ = 1.0f;
    currentBlockTopApplied_ = false;
    currentBlock_ = makeUniqueNoThrow<ParsedText>(spec_.focusReadingEnabled, spec_.guideDotsMode, style,
                                                  spec_.paragraphIndentSpaces, spec_.wordSpacing);
    if (!currentBlock_) return;
    blockKind_ = BlockKind::Paragraph;
  }
  appendInlineText(*currentBlock_, trimmed, EpdFontFamily::REGULAR, /*allowSoftFlush=*/true);
}

void MarkdownParser::processLineContinuation(const std::string_view rawChunk, const bool isLineEnd) {
  const std::string_view chunk = isLineEnd ? trimRight(rawChunk) : rawChunk;
  if (inCodeFence_) {
    if (blockKind_ == BlockKind::CodeLine) {
      appendCodeText(chunk);
      if (isLineEnd) flushOpenBlock();
    }
    return;
  }
  if (!currentBlock_) return;

  if (blockKind_ == BlockKind::Heading) {
    appendInlineText(*currentBlock_, chunk, EpdFontFamily::BOLD, /*allowSoftFlush=*/true);
    if (isLineEnd) flushOpenBlock();
    return;
  }
  appendInlineText(*currentBlock_, chunk, EpdFontFamily::REGULAR, /*allowSoftFlush=*/true);
}

void MarkdownParser::maybeYieldAndReport(const uint32_t bytesRead, const uint32_t totalBytes, const bool force) {
  const bool pageTrigger = static_cast<uint32_t>(completedPageCount_) >= lastYieldPage_ + YIELD_PAGE_INTERVAL;
  const bool lineTrigger = linesProcessed_ > 0 && (linesProcessed_ % YIELD_LINE_INTERVAL) == 0;
  if (!force && !pageTrigger && !lineTrigger) return;
  lastYieldPage_ = static_cast<uint32_t>(completedPageCount_);
  yieldCount_++;
  yieldToScheduler();
  if (progressFn_) progressFn_(progressCtx_, bytesRead, totalBytes);
}

bool MarkdownParser::parseAndBuildPages() {
  HalFile file;
  if (!Storage.openFileForRead("MDP", filepath_, file)) {
    return false;
  }

  auto chunk = makeUniqueNoThrow<char[]>(READ_CHUNK_BYTES);
  if (!chunk) {
    LOG_ERR("MDP", "OOM: read chunk");
    return false;
  }

  const uint32_t totalBytes = static_cast<uint32_t>(file.size());
  uint32_t bytesRead = 0;

  std::string lineBuf;
  lineBuf.reserve(256);
  bool firstChunk = true;
  bool lineContinued = false;

  for (;;) {
    const int n = file.read(chunk.get(), READ_CHUNK_BYTES);
    if (n <= 0) break;
    bytesRead += static_cast<uint32_t>(n);
    int start = 0;
    if (firstChunk) {
      firstChunk = false;
      if (n >= 3 && static_cast<uint8_t>(chunk[0]) == 0xEF && static_cast<uint8_t>(chunk[1]) == 0xBB &&
          static_cast<uint8_t>(chunk[2]) == 0xBF) {
        start = 3;
      }
    }
    for (int i = start; i < n; ++i) {
      const char c = chunk[i];
      if (c == '\n') {
        if (!lineContinued) {
          processLineStart(lineBuf, /*isLineEnd=*/true);
        } else {
          processLineContinuation(lineBuf, /*isLineEnd=*/true);
        }
        lineBuf.clear();
        lineContinued = false;
        linesProcessed_++;
        maybeYieldAndReport(bytesRead, totalBytes, false);
      } else {
        lineBuf.push_back(c);
        if (lineBuf.size() >= LINE_CHUNK_FLUSH_BYTES) {
          const size_t split = findSafeChunkSplit(lineBuf);
          const std::string_view head(lineBuf.data(), split);
          if (!lineContinued) {
            processLineStart(head, /*isLineEnd=*/false);
            lineContinued = true;
          } else {
            processLineContinuation(head, /*isLineEnd=*/false);
          }
          lineBuf.erase(0, split);
          linesProcessed_++;
          maybeYieldAndReport(bytesRead, totalBytes, false);
        }
      }
    }
  }
  if (!lineBuf.empty() || lineContinued) {
    if (!lineContinued) {
      processLineStart(lineBuf, /*isLineEnd=*/true);
    } else {
      processLineContinuation(lineBuf, /*isLineEnd=*/true);
    }
    lineBuf.clear();
  }
  flushOpenBlock();
  if (currentPage_ && !currentPage_->elements.empty()) {
    emitCurrentPage();
  }
  maybeYieldAndReport(totalBytes, totalBytes, /*force=*/true);
  return true;
}
