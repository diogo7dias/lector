#pragma once

#include <ArenaVector.h>
#include <EpdFontFamily.h>

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  // Word text lives in one arena-backed char pool (textPool_) instead of one
  // heap std::string per word: hundreds of small string allocations per
  // paragraph interleaved with glyph-cache allocations and fragmented the
  // layout-time heap. Every view in `words` (and the per-line scratch below)
  // points into the pool and is guaranteed NUL-terminated by poolStore(), so
  // .data() is safe at C API boundaries.
  std::vector<std::string_view> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  std::vector<bool> wordContinues;      // true = word attaches to previous with no break
  std::vector<bool> wordNoSpaceBefore;  // true = may break before token, but no synthetic space when joined
  std::vector<uint8_t>
      wordFocusBoundary;  // UTF-8 byte offset where the regular suffix of a focus-split word starts; 0 = no split
  std::vector<bool> wordGuideDotBefore;  // true = a virtual guide dot belongs in the gap before this token
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool guideReadingEnabled;
  bool isNaturalAlign;
  bool hasRtlWord;
  // Explicit first-line indent in pixels. -1 = "book mode" (use the publisher/CSS
  // text-indent or the default fallback). >= 0 overrides it: 0 = flush, larger =
  // wider first-line indent. Pre-computed by the caller from the indent-percent
  // setting and the viewport width, so no percentage math happens here.
  int firstLineIndentPx;
  // Word spacing as a 10%-step count (3 = 0%). Adds a signed delta to every real
  // inter-word gap so line-breaking, justification and drawing stay consistent.
  uint8_t wordSpacing;
  std::vector<std::string_view> reorderedWordsScratch;
  std::vector<EpdFontFamily::Style> reorderedStylesScratch;
  std::vector<uint16_t> reorderedWidthsScratch;
  std::vector<bool> reorderedContinuesScratch;
  std::vector<bool> reorderedNoSpaceBeforeScratch;
  std::vector<uint8_t> reorderedFocusBoundaryScratch;
  std::vector<uint16_t> visualOrderScratch;
  // Per-line scratch reused across extractLine() calls: cleared (not reallocated)
  // each line so a page of ~25-30 lines reuses one set of buffers instead of
  // allocating fresh vectors per line. lineWords/lineStyles/lineXPos build the
  // line; the out* buffers hold the focus-merged result on the slow path.
  std::vector<std::string_view> lineWordsScratch;
  std::vector<EpdFontFamily::Style> lineStylesScratch;
  std::vector<int16_t> lineXPosScratch;
  std::vector<std::string_view> outWordsScratch;
  std::vector<int16_t> outXPosScratch;
  std::vector<EpdFontFamily::Style> outStylesScratch;
  std::vector<uint8_t> outBoundaryScratch;
  std::vector<uint16_t> outSuffixXScratch;
  std::vector<uint16_t> outGuideDotXOffsetScratch;

  // Bump-allocated char pool holding every word's text for this paragraph's
  // lifetime; freed as one block when the ParsedText is destroyed (a ParsedText
  // is per-paragraph and dies right after layoutAndExtractLines).
  Arena textPool_;
  // Copies [data, data+len) into textPool_ with a trailing NUL and returns a
  // view of it. On pool OOM returns a default view (data() == nullptr); the
  // caller drops the token/paragraph rather than crashing.
  std::string_view poolStore(const char* data, size_t len);

  int resolveFirstLineIndent(bool isFirstLine, const GfxRenderer& renderer, int fontId) const;
  // Signed pixels to add to each real inter-word gap for the word-spacing setting.
  int wordSpacingDeltaPx(const GfxRenderer& renderer, int fontId) const;
  // Line-breaking scratch (word widths, cost tables, break indices) is backed by
  // a per-paragraph Arena instead of std::vector so the heap sees one slab
  // alloc/free per paragraph rather than several churning allocations. Returns
  // false only on arena OOM; the caller drops the paragraph rather than abort().
  bool computeLineBreaks(Arena& scratch, const GfxRenderer& renderer, int fontId, int pageWidth,
                         ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                         std::vector<bool>& noSpaceBeforeVec, ArenaVector<size_t>& lineBreakIndices);
  bool computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                   ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                   std::vector<bool>& noSpaceBeforeVec, ArenaVector<size_t>& lineBreakIndices);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            ArenaVector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  void extractLine(size_t breakIndex, int pageWidth, const ArenaVector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                   const ArenaVector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine, const GfxRenderer& renderer,
                   int fontId);
  bool calculateWordWidths(ArenaVector<uint16_t>& wordWidths, const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool hyphenationEnabled = false,
                      const bool focusReadingEnabled = false, const bool guideReadingEnabled = false,
                      const BlockStyle& blockStyle = BlockStyle(), const int firstLineIndentPx = -1,
                      const uint8_t wordSpacing = 3)
      : blockStyle(blockStyle),
        extraParagraphSpacing(extraParagraphSpacing),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        guideReadingEnabled(guideReadingEnabled),
        isNaturalAlign(false),
        hasRtlWord(false),
        firstLineIndentPx(firstLineIndentPx),
        wordSpacing(wordSpacing) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false);
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
};
