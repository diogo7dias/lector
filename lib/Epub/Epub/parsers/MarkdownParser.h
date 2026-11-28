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
// Reads the source file in 1 KB chunks without buffering the document in RAM.
class MarkdownParser {
 public:
  static constexpr size_t MAX_TOC_ENTRIES = 128;

  MarkdownParser(const std::string& filepath, GfxRenderer& renderer, const ReaderRenderSpec& spec,
                 PageCompleteFn completePageFn, void* completePageCtx);
  ~MarkdownParser();

  bool parseAndBuildPages();
  const std::vector<MarkdownTocEntry>& getToc() const { return toc_; }

  // Strip inline markdown markers (**, *, [label](url)) into plain text for TOC titles.
  static std::string stripInlineMarkdown(std::string_view text);

 private:
  enum class BlockKind : uint8_t { None, Paragraph, Quote, ListItem };

  const std::string& filepath_;
  GfxRenderer& renderer_;
  ReaderRenderSpec spec_;
  PageCompleteFn completePageFn_;
  void* completePageCtx_;

  std::unique_ptr<Page> currentPage_;
  std::unique_ptr<ParsedText> currentBlock_;
  BlockKind blockKind_ = BlockKind::None;
  int16_t currentPageNextY_ = 0;
  int completedPageCount_ = 0;
  uint16_t paragraphOrdinal_ = 0;
  bool pendingParagraphFirstLine_ = false;

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

  std::vector<MarkdownTocEntry> toc_;

  void processLine(std::string_view line);
  void flushOpenBlock();
  void emitHeading(uint8_t level, std::string_view text);
  void appendInlineText(ParsedText& target, std::string_view text, uint8_t baseStyle);
  void layoutBlock(ParsedText& block, float fontScale, bool isQuote);
  void addLineToPage(std::unique_ptr<TextBlock> line, bool isQuote);
  void closeQuoteBoxSlice();
  void emitCurrentPage();
  bool ensurePage();
};
