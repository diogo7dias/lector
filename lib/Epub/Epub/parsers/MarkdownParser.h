#pragma once

#include <Epub/PageCallbacks.h>
#include <Epub/ReaderRenderSpec.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class GfxRenderer;
class Page;
class ParsedText;
class TextBlock;

struct MarkdownTocEntry {
  std::string title;
  uint16_t pageIndex = 0;
  uint8_t level = 1;  // 1 = '#', 2 = '##'
};

// Streaming line-by-line Markdown parser that feeds ParsedText / Page / TextBlock.
// Reads the source file in 1 KB chunks, streams arbitrarily long physical lines without
// dropping bytes, soft-flushes long paragraphs at SOFT_FLUSH_WORDS (750, matching the
// EPUB parser) to keep C3 RAM bounded, and yields cooperatively during indexing.
class MarkdownParser {
 public:
  static constexpr size_t MAX_TOC_ENTRIES = 128;
  static constexpr size_t SOFT_FLUSH_WORDS = 750;

  using ProgressFn = void (*)(void* ctx, uint32_t bytesRead, uint32_t totalBytes);

  MarkdownParser(const std::string& filepath, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                 PageCompleteFn completePageFn, void* completePageCtx, ProgressFn progressFn = nullptr,
                 void* progressCtx = nullptr);
  ~MarkdownParser();

  bool parseAndBuildPages();
  const std::vector<MarkdownTocEntry>& getToc() const { return toc_; }

  // Diagnostics for unit tests (heap cap verification and cooperative yield cadence).
  size_t maxBufferedWords() const { return maxBufferedWords_; }
  size_t softFlushCount() const { return softFlushCount_; }
  uint32_t yieldCount() const { return yieldCount_; }

  // Strip inline markdown markers (**, *, `, [label](url)) into plain text for TOC titles.
  static std::string stripInlineMarkdown(std::string_view text);

 private:
  enum class BlockKind : uint8_t { None, Paragraph, Quote, ListItem, Heading, CodeLine };

  const std::string& filepath_;
  GfxRenderer& renderer_;
  ReaderRenderSpec spec_;
  PageCompleteFn completePageFn_;
  void* completePageCtx_;
  ProgressFn progressFn_;
  void* progressCtx_;

  std::unique_ptr<Page> currentPage_;
  std::unique_ptr<ParsedText> currentBlock_;
  BlockKind blockKind_ = BlockKind::None;
  float currentBlockScale_ = 1.0f;
  bool currentBlockTopApplied_ = false;
  bool inlineBold_ = false;
  bool inlineItalic_ = false;
  int16_t currentPageNextY_ = 0;
  int completedPageCount_ = 0;
  uint16_t paragraphOrdinal_ = 0;
  bool pendingParagraphFirstLine_ = false;

  // Fenced code block state (``` or ~~~).
  bool inCodeFence_ = false;
  char fenceChar_ = '`';
  uint8_t fenceLen_ = 0;

  // Vertical bar for open blockquote spans on the current page.
  bool inQuoteBox_ = false;
  int16_t quoteBoxX_ = 0;
  int16_t quoteBoxWidth_ = 0;
  int16_t quoteBoxTop_ = -1;
  int16_t quoteBoxBottom_ = -1;

  // Pending TOC heading whose page index is captured when its first line lands.
  bool pendingToc_ = false;
  uint8_t pendingTocLevel_ = 1;
  std::string pendingTocTitle_;

  // Yield / progress cadence and test counters.
  uint32_t linesProcessed_ = 0;
  uint32_t lastYieldPage_ = 0;
  uint32_t yieldCount_ = 0;
  size_t maxBufferedWords_ = 0;
  size_t softFlushCount_ = 0;

  std::vector<MarkdownTocEntry> toc_;

  void processLineStart(std::string_view chunk, bool isLineEnd);
  void processLineContinuation(std::string_view chunk, bool isLineEnd);
  void processCodeLineStart(std::string_view chunk, bool isLineEnd);
  void appendCodeText(std::string_view text);
  void beginHeading(uint8_t level, std::string_view text);
  void flushOpenBlock();
  void softFlushOpenBlock();
  void appendInlineText(ParsedText& target, std::string_view text, uint8_t baseStyle, bool allowSoftFlush);
  void layoutBlock(ParsedText& block, float fontScale, bool isQuote, bool isCode, bool includeLastLine);
  void addLineToPage(std::unique_ptr<TextBlock> line, bool isQuote);
  void closeQuoteBoxSlice();
  void emitCurrentPage();
  bool ensurePage();
  void maybeYieldAndReport(uint32_t bytesRead, uint32_t totalBytes, bool force);
};
