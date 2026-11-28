#include "MarkdownParser.h"

#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssStyle.h"

namespace {
constexpr size_t READ_CHUNK_BYTES = 1024;
constexpr size_t MAX_LINE_BYTES = 4096;
constexpr int16_t QUOTE_BAR_THICKNESS = 3;
constexpr int16_t QUOTE_BAR_GAP = 8;

bool isAsciiSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view trimLeft(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
  return s;
}

std::string_view trimRight(std::string_view s) {
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
  return s;
}

// Parse ATX heading ("# Title" .. "###### Title"). Returns level 1..6, or 0 if not a heading.
uint8_t parseHeadingPrefix(std::string_view line, std::string_view& outText) {
  size_t hashes = 0;
  while (hashes < line.size() && hashes < 6 && line[hashes] == '#') ++hashes;
  if (hashes == 0 || hashes >= line.size() || line[hashes] != ' ') return 0;
  std::string_view body = trimRight(trimLeft(line.substr(hashes + 1)));
  // Strip optional closing hashes ("## Title ##").
  while (!body.empty() && body.back() == '#') body.remove_suffix(1);
  body = trimRight(body);
  if (body.empty()) return 0;
  outText = body;
  return static_cast<uint8_t>(hashes);
}

// Parse unordered list item ("- text", "* text", "+ text").
bool parseBulletPrefix(std::string_view line, std::string_view& outText) {
  const std::string_view trimmed = trimLeft(line);
  if (trimmed.size() < 2) return false;
  if ((trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') && trimmed[1] == ' ') {
    outText = trimLeft(trimmed.substr(2));
    return !outText.empty();
  }
  return false;
}

// Parse ordered list item ("1. text", "12. text").
bool parseOrderedPrefix(std::string_view line, std::string& outMarker, std::string_view& outText) {
  const std::string_view trimmed = trimLeft(line);
  size_t digits = 0;
  while (digits < trimmed.size() && digits < 6 && std::isdigit(static_cast<unsigned char>(trimmed[digits]))) {
    ++digits;
  }
  if (digits == 0 || digits + 1 >= trimmed.size()) return false;
  if (trimmed[digits] != '.' || trimmed[digits + 1] != ' ') return false;
  outText = trimLeft(trimmed.substr(digits + 2));
  if (outText.empty()) return false;
  outMarker.assign(trimmed.data(), digits + 1);  // "1."
  return true;
}

// Parse link "[label](url)" starting at `pos` (where text[pos] == '[').
// On match, fills `label` and advances `outEnd` past the closing ')'.
bool matchInlineLink(std::string_view text, const size_t pos, std::string_view& label, size_t& outEnd) {
  if (pos >= text.size() || text[pos] != '[') return false;
  size_t closeBracket = std::string_view::npos;
  for (size_t i = pos + 1; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text[i] == ']') {
      closeBracket = i;
      break;
    }
  }
  if (closeBracket == std::string_view::npos || closeBracket + 1 >= text.size() || text[closeBracket + 1] != '(') {
    return false;
  }
  int parenDepth = 1;
  size_t i = closeBracket + 2;
  for (; i < text.size() && parenDepth > 0; ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text[i] == '(')
      ++parenDepth;
    else if (text[i] == ')')
      --parenDepth;
  }
  if (parenDepth != 0) return false;
  label = text.substr(pos + 1, closeBracket - pos - 1);
  outEnd = i;
  return true;
}

// Check if delimiter `delim` has a matching closing occurrence before end of `text`.
bool hasClosingDelimiter(std::string_view text, const size_t afterOpen, std::string_view delim) {
  if (afterOpen >= text.size() || isAsciiSpace(text[afterOpen])) return false;
  for (size_t i = afterOpen; i + delim.size() <= text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      continue;
    }
    if (text.substr(i, delim.size()) == delim && i > afterOpen && !isAsciiSpace(text[i - 1])) {
      return true;
    }
  }
  return false;
}

CssTextAlign resolveParagraphAlign(const uint8_t alignment) {
  if (alignment == static_cast<uint8_t>(CssTextAlign::None)) return CssTextAlign::Justify;
  return static_cast<CssTextAlign>(alignment);
}

}  // namespace

MarkdownParser::MarkdownParser(const std::string& filepath, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                               const PageCompleteFn completePageFn, void* const completePageCtx)
    : filepath_(filepath),
      renderer_(renderer),
      spec_(spec),
      completePageFn_(completePageFn),
      completePageCtx_(completePageCtx) {}

MarkdownParser::~MarkdownParser() = default;

std::string MarkdownParser::stripInlineMarkdown(const std::string_view text) {
  std::string out;
  out.reserve(text.size());
  size_t i = 0;
  bool bold = false;
  bool italic = false;
  while (i < text.size()) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      out.push_back(text[i + 1]);
      i += 2;
      continue;
    }
    if (text[i] == '[') {
      std::string_view label;
      size_t endPos = 0;
      if (matchInlineLink(text, i, label, endPos)) {
        out += stripInlineMarkdown(label);
        i = endPos;
        continue;
      }
    }
    if (i + 2 < text.size() && text.substr(i, 3) == "***") {
      if ((bold && italic) || hasClosingDelimiter(text, i + 3, "***")) {
        bold = !bold;
        italic = !italic;
        i += 3;
        continue;
      }
    }
    if (i + 1 < text.size() && text.substr(i, 2) == "**") {
      if (bold || hasClosingDelimiter(text, i + 2, "**")) {
        bold = !bold;
        i += 2;
        continue;
      }
    }
    if (text[i] == '*') {
      if (italic || hasClosingDelimiter(text, i + 1, "*")) {
        italic = !italic;
        i += 1;
        continue;
      }
    }
    out.push_back(text[i++]);
  }
  return out;
}

bool MarkdownParser::ensurePage() {
  if (currentPage_) return true;
  currentPage_ = makeUniqueNoThrow<Page>();
  if (!currentPage_) {
    LOG_ERR("MDP", "OOM: Page");
    return false;
  }
  currentPageNextY_ = 0;
  return true;
}

void MarkdownParser::closeQuoteBoxSlice() {
  if (!inQuoteBox_ || quoteBoxTop_ < 0 || quoteBoxBottom_ <= quoteBoxTop_ || !currentPage_) {
    quoteBoxTop_ = quoteBoxBottom_ = -1;
    return;
  }
  CssBorderSide sides[4] = {};
  sides[3] = {static_cast<uint8_t>(QUOTE_BAR_THICKNESS), CssBorderStyle::Solid};
  const auto height = static_cast<uint16_t>(quoteBoxBottom_ - quoteBoxTop_);
  auto box = makeUniqueNoThrow<PageBorderBox>(static_cast<uint16_t>(quoteBoxWidth_), height, sides, false, quoteBoxX_,
                                              quoteBoxTop_);
  if (box) {
    currentPage_->elements.push_back(std::move(box));
  } else {
    LOG_ERR("MDP", "OOM: quote bar");
  }
  quoteBoxTop_ = quoteBoxBottom_ = -1;
}

void MarkdownParser::emitCurrentPage() {
  if (!currentPage_) return;
  closeQuoteBoxSlice();
  currentPage_->centerFullTextPage(renderer_, spec_.fontId, spec_.lineCompression, spec_.viewportHeight);
  completePageFn_(completePageCtx_, std::move(currentPage_), 0, 0, 0);
  completedPageCount_++;
  currentPageNextY_ = 0;
}

void MarkdownParser::addLineToPage(std::unique_ptr<TextBlock> line, const bool isQuote) {
  if (!ensurePage()) return;
  const int lineFontId = line->blockFontId(spec_.fontId);
  const int lineHeight =
      renderer_.getLineHeight(lineFontId, spec_.lineCompression) + line->extraAscent(renderer_, spec_.fontId);

  if (!currentPage_->elements.empty() && currentPageNextY_ + lineHeight > spec_.viewportHeight) {
    emitCurrentPage();
    if (!ensurePage()) return;
  }

  if (pendingToc_) {
    if (toc_.size() < MAX_TOC_ENTRIES) {
      toc_.push_back({std::move(pendingTocTitle_), static_cast<uint16_t>(completedPageCount_), pendingTocLevel_});
    }
    pendingToc_ = false;
    pendingTocTitle_.clear();
  }

  const int16_t xOffset = line->getBlockStyle().leftInset();
  if (line->wordCount() > 0) {
    auto pageLine = makeUniqueNoThrow<PageLine>(std::move(line), xOffset, currentPageNextY_);
    if (!pageLine) {
      LOG_ERR("MDP", "OOM: PageLine");
      return;
    }
    if (pendingParagraphFirstLine_) {
      paragraphOrdinal_++;
      pageLine->setParagraphOrdinal(paragraphOrdinal_);
      pendingParagraphFirstLine_ = false;
    }
    currentPage_->elements.push_back(std::move(pageLine));
  }

  if (isQuote && inQuoteBox_) {
    if (quoteBoxTop_ < 0) quoteBoxTop_ = currentPageNextY_;
    quoteBoxBottom_ = static_cast<int16_t>(currentPageNextY_ + lineHeight);
  }
  currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + lineHeight);
}

void MarkdownParser::layoutBlock(ParsedText& block, const float fontScale, const bool isQuote) {
  if (block.isEmpty()) return;
  if (!ensurePage()) return;

  const int variant = renderer_.resolveFontVariant(spec_.fontId, fontScale);
  const int layoutFontId = variant != 0 ? variant : spec_.fontId;
  BlockStyle& style = block.getBlockStyle();
  style.fontId = layoutFontId == spec_.fontId ? 0 : layoutFontId;

  const int horizontalInset = style.totalHorizontalInset();
  const uint16_t effectiveWidth = horizontalInset < spec_.viewportWidth
                                      ? static_cast<uint16_t>(spec_.viewportWidth - horizontalInset)
                                      : spec_.viewportWidth;

  bool topApplied = false;
  const auto applyTop = [&]() {
    if (topApplied) return;
    if (currentPage_ && !currentPage_->elements.empty()) {
      currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + style.topInset());
    }
    topApplied = true;
  };

  pendingParagraphFirstLine_ = !block.getIsHeading() && block.hasLetters();
  block.layoutAndExtractLines(
      renderer_, layoutFontId, effectiveWidth,
      [&](std::unique_ptr<TextBlock> line, uint32_t) {
        applyTop();
        addLineToPage(std::move(line), isQuote);
      },
      true);

  const int baseLineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
  int bottomGap = style.bottomInset();
  if (spec_.extraParagraphSpacing && !isQuote && !block.getIsHeading()) {
    bottomGap += baseLineH / 2;
  }
  currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + bottomGap);
}

void MarkdownParser::appendInlineText(ParsedText& target, const std::string_view text, const uint8_t baseStyle) {
  std::string word;
  word.reserve(48);
  bool bold = (baseStyle & EpdFontFamily::BOLD) != 0;
  bool italic = (baseStyle & EpdFontFamily::ITALIC) != 0;
  bool nextContinues = false;

  const auto currentStyle = [&]() -> EpdFontFamily::Style {
    uint8_t s = baseStyle;
    if (bold) s |= EpdFontFamily::BOLD;
    if (italic) s |= EpdFontFamily::ITALIC;
    return static_cast<EpdFontFamily::Style>(s);
  };

  const auto flushWord = [&](const bool attachNext) {
    if (!word.empty()) {
      target.addWord(word, currentStyle(), nextContinues, 0);
      word.clear();
      nextContinues = attachNext;
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
    if (c == '[') {
      std::string_view label;
      size_t endPos = 0;
      if (matchInlineLink(text, i, label, endPos)) {
        const bool hadWordBefore = !word.empty();
        flushWord(hadWordBefore);
        const bool attachLinkStart = nextContinues;
        const size_t beforeCount = target.size();
        appendInlineText(target, label, static_cast<uint8_t>(currentStyle()));
        // If link text was immediately preceded by characters without whitespace, keep attachment.
        (void)attachLinkStart;
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
}

void MarkdownParser::emitHeading(const uint8_t level, const std::string_view text) {
  flushOpenBlock();
  const int lineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.marginTop = static_cast<int16_t>(level == 1 ? lineH / 2 : lineH / 3);
  style.marginBottom = static_cast<int16_t>(lineH / 3);

  static constexpr float SCALES[] = {1.5f, 1.2f, 1.0f, 1.0f, 1.0f, 1.0f};
  const float scale = SCALES[std::clamp<uint8_t>(level, 1, 6) - 1];

  auto block = makeUniqueNoThrow<ParsedText>(false, GUIDE_DOTS_OFF, style, 0, spec_.wordSpacing);
  if (!block) {
    LOG_ERR("MDP", "OOM: heading block");
    return;
  }
  block->setHeading(true);
  block->suppressFirstLineIndent();
  appendInlineText(*block, text, EpdFontFamily::BOLD);
  if (block->isEmpty()) return;

  if (level <= 2) {
    pendingToc_ = true;
    pendingTocLevel_ = level;
    pendingTocTitle_ = stripInlineMarkdown(text);
  }
  layoutBlock(*block, scale, false);
}

void MarkdownParser::flushOpenBlock() {
  if (!currentBlock_) return;
  const bool isQuote = blockKind_ == BlockKind::Quote;
  layoutBlock(*currentBlock_, 1.0f, isQuote);
  currentBlock_.reset();
  if (isQuote) {
    closeQuoteBoxSlice();
    inQuoteBox_ = false;
    const int baseLineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
    if (spec_.extraParagraphSpacing) {
      currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + baseLineH / 2);
    }
  }
  blockKind_ = BlockKind::None;
}

void MarkdownParser::processLine(const std::string_view rawLine) {
  const std::string_view line = trimRight(rawLine);
  if (trimLeft(line).empty()) {
    flushOpenBlock();
    return;
  }

  // 1. ATX Headings (# / ## / ### ...)
  std::string_view headingText;
  if (const uint8_t level = parseHeadingPrefix(line, headingText); level > 0) {
    emitHeading(level, headingText);
    return;
  }

  // 2. Blockquote (> text)
  const std::string_view trimmed = trimLeft(line);
  if (trimmed[0] == '>') {
    std::string_view quoteBody = trimmed.substr(1);
    if (!quoteBody.empty() && quoteBody.front() == ' ') quoteBody.remove_prefix(1);
    if (trimLeft(quoteBody).empty()) {
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
    appendInlineText(*currentBlock_, quoteBody, EpdFontFamily::REGULAR);
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
    currentBlock_ =
        makeUniqueNoThrow<ParsedText>(spec_.focusReadingEnabled, spec_.guideDotsMode, style, 0, spec_.wordSpacing);
    if (!currentBlock_) return;
    currentBlock_->suppressFirstLineIndent();
    currentBlock_->addWord(isBullet ? "\xe2\x80\xa2" : orderedMarker, EpdFontFamily::REGULAR, false, 0);
    appendInlineText(*currentBlock_, itemText, EpdFontFamily::REGULAR);
    blockKind_ = BlockKind::ListItem;
    return;
  }

  // 4. Plain paragraph line (or continuation line of open paragraph / list item)
  if (blockKind_ == BlockKind::Quote) {
    flushOpenBlock();
  }
  if (!currentBlock_) {
    BlockStyle style;
    style.alignment = resolveParagraphAlign(spec_.paragraphAlignment);
    currentBlock_ = makeUniqueNoThrow<ParsedText>(spec_.focusReadingEnabled, spec_.guideDotsMode, style,
                                                  spec_.paragraphIndentSpaces, spec_.wordSpacing);
    if (!currentBlock_) return;
    blockKind_ = BlockKind::Paragraph;
  }
  appendInlineText(*currentBlock_, trimmed, EpdFontFamily::REGULAR);
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

  std::string lineBuf;
  lineBuf.reserve(256);
  bool firstChunk = true;

  for (;;) {
    const int n = file.read(chunk.get(), READ_CHUNK_BYTES);
    if (n <= 0) break;
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
        processLine(lineBuf);
        lineBuf.clear();
      } else if (lineBuf.size() < MAX_LINE_BYTES) {
        lineBuf.push_back(c);
      }
    }
  }
  if (!lineBuf.empty()) {
    processLine(lineBuf);
    lineBuf.clear();
  }
  flushOpenBlock();
  if (currentPage_ && !currentPage_->elements.empty()) {
    emitCurrentPage();
  }
  return true;
}
