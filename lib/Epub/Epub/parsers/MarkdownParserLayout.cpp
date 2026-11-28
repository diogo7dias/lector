#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>

#include "Epub/Page.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/BlockStyle.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssStyle.h"
#include "MarkdownParser.h"

namespace {
constexpr int16_t QUOTE_BAR_THICKNESS = 3;
}  // namespace

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

void MarkdownParser::layoutBlock(ParsedText& block, const float fontScale, const bool isQuote, const bool isCode,
                                 const bool includeLastLine) {
  if (block.isEmpty()) return;
  if (!ensurePage()) return;

  int layoutFontId = spec_.fontId;
  if (isCode) {
    const int mono = renderer_.resolveMonospaceFont(spec_.fontId, fontScale);
    layoutFontId = mono != 0 ? mono : spec_.fontId;
  } else {
    const int variant = renderer_.resolveFontVariant(spec_.fontId, fontScale);
    layoutFontId = variant != 0 ? variant : spec_.fontId;
  }

  // Resolve inline size / monospace slots (e.g. inline `code` spans), matching ChapterHtmlSlimParser.
  for (uint8_t slot = 1; slot < block.sizeSlotsInUse(); ++slot) {
    const float slotScale = block.sizeSlotScale(slot);
    const int mono = block.sizeSlotIsMonospace(slot) ? renderer_.resolveMonospaceFont(spec_.fontId, slotScale) : 0;
    const int scaled = renderer_.resolveFontVariant(spec_.fontId, slotScale);
    block.setSizeSlotFontId(slot, mono != 0 ? mono : (scaled != 0 ? scaled : spec_.fontId));
  }

  BlockStyle& style = block.getBlockStyle();
  style.fontId = layoutFontId == spec_.fontId ? 0 : layoutFontId;

  const int horizontalInset = style.totalHorizontalInset();
  const uint16_t effectiveWidth = horizontalInset < spec_.viewportWidth
                                      ? static_cast<uint16_t>(spec_.viewportWidth - horizontalInset)
                                      : spec_.viewportWidth;

  const auto applyTop = [&]() {
    if (currentBlockTopApplied_) return;
    if (currentPage_ && !currentPage_->elements.empty()) {
      currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + style.topInset());
    }
    currentBlockTopApplied_ = true;
  };

  if (!currentBlockTopApplied_) {
    pendingParagraphFirstLine_ = !block.getIsHeading() && !isCode && block.hasLetters();
  }
  block.layoutAndExtractLines(
      renderer_, layoutFontId, effectiveWidth,
      [&](std::unique_ptr<TextBlock> line, uint32_t) {
        applyTop();
        addLineToPage(std::move(line), isQuote);
      },
      includeLastLine);

  if (includeLastLine) {
    const int baseLineH = renderer_.getLineHeight(spec_.fontId, spec_.lineCompression);
    int bottomGap = style.bottomInset();
    if (spec_.extraParagraphSpacing && !isQuote && !isCode && !block.getIsHeading()) {
      bottomGap += baseLineH / 2;
    }
    currentPageNextY_ = static_cast<int16_t>(currentPageNextY_ + bottomGap);
  }
}

void MarkdownParser::softFlushOpenBlock() {
  if (!currentBlock_ || currentBlock_->isEmpty()) return;
  const bool isQuote = blockKind_ == BlockKind::Quote;
  const bool isCode = blockKind_ == BlockKind::CodeLine;
  layoutBlock(*currentBlock_, currentBlockScale_, isQuote, isCode, /*includeLastLine=*/false);
  softFlushCount_++;
}

void MarkdownParser::flushOpenBlock() {
  if (!currentBlock_) {
    blockKind_ = BlockKind::None;
    inlineBold_ = false;
    inlineItalic_ = false;
    return;
  }
  const bool isQuote = blockKind_ == BlockKind::Quote;
  const bool isCode = blockKind_ == BlockKind::CodeLine;
  layoutBlock(*currentBlock_, currentBlockScale_, isQuote, isCode, /*includeLastLine=*/true);
  currentBlock_.reset();
  currentBlockTopApplied_ = false;
  currentBlockScale_ = 1.0f;
  inlineBold_ = false;
  inlineItalic_ = false;
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
