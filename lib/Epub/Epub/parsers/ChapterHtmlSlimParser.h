#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <array>
#include <climits>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ArxHash.h"
#include "Epub/FootnoteEntry.h"
#include "Epub/PageCallbacks.h"
#include "Epub/ParsedText.h"
#include "Epub/ReaderRenderSpec.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"
#include "VoidTagFixer.h"

inline uint64_t arxHash64(const std::string& s) { return arxHash64(s.c_str()); }

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  // Null for HTML that is not a book page (the dictionary renderer), which is why this
  // is a pointer rather than a reference.
  Epub* epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  // Function pointer plus context rather than std::function: the parser is built once per
  // chapter on the OOM-sensitive render path, and each std::function costs a heap-allocated
  // closure plus its own copy of the call machinery in flash.
  PageCompleteFn completePageFn;
  void* completePageCtx;
  PopupFn popupFn;  // Popup callback, may be null
  void* popupCtx;
  bool imagePopupFired = false;  // popupFn fired for the first image probe (single-shot)
  int depth = 0;
  int skipUntilDepth = INT_MAX;
  int boldUntilDepth = INT_MAX;
  int italicUntilDepth = INT_MAX;
  // buffer for building up words from characters, will auto break if longer than this
  // leave one char at end for null pointer
  char partWordBuffer[MAX_WORD_SIZE + 1] = {};
  int partWordBufferIndex = 0;
  bool nextWordContinues = false;  // true when next flushed word attaches to previous (inline element boundary)
  std::unique_ptr<ParsedText> currentTextBlock = nullptr;
  // Ruby text state
  bool inRuby = false;
  int rubyStartWordIndex = -1;
  bool collectingRubyText = false;
  std::string rubyTextBuffer;
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t wordSpacing;
  uint8_t paragraphSpacing;  // extra block gap after each paragraph, % of line height
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool focusReadingEnabled;
  uint8_t guideDotsMode;  // GuideDotsMode: off / visible dots / hidden dots (gap only)
  uint8_t firstLineIndentMode;
  uint8_t firstLineIndentPercent;
  bool linkUnderline;  // underline in-book links; off leaves them plain
  const CssParser* cssParser;
  bool embeddedTextStyle;
  bool embeddedLayoutStyle;
  bool bookMargins;
  uint8_t wordExpansion;              // Word Expansion cap, px between letters
  bool headingPageBreak;              // h1/h2 start a new page
  bool lastBlockWasHeading_ = false;  // the last block laid out was a heading
  bool breakBeforeBlock_ = false;     // the block being opened is an h1/h2 to break before
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;
  // Small images laid out inside the open paragraph, waiting for their line to land.
  static constexpr size_t MAX_PENDING_INLINE_IMAGES = 16;
  struct PendingInlineImage {
    uint16_t id;
    std::unique_ptr<ImageBlock> block;
  };
  std::vector<PendingInlineImage> pendingInlineImages;
  uint16_t nextInlineImageId = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool setsParagraphDirection = false;
    bool hasTextAlign = false;
    CssTextAlign textAlign = CssTextAlign::Left;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
    bool hasSmallCaps = false, smallCaps = false;
    bool hasWhiteSpace = false, preserveWhitespace = false;
    // Inline font size: a multiple of the parent size, or of the body size when rem.
    bool hasFontScale = false, fontScaleRem = false;
    float fontScale = 1.0f;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  // ponytail: hard cap on nested inline styles. A hostile chapter of thousands of nested
  // <span style> grew this vector until the C3 aborted, and every push rescans the whole
  // stack. Deeper elements keep their parent's style; pops match on depth, so a skipped
  // push never pops a parent's entry.
  static constexpr size_t MAX_INLINE_STYLE_DEPTH = 64;
  void pushInlineStyle(const StyleStackEntry& entry) {
    if (inlineStyleStack.size() < MAX_INLINE_STYLE_DEPTH) inlineStyleStack.push_back(entry);
  }
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  // Open elements by depth, for descendant/child CSS selectors. Deeper elements
  // resolve without ancestors, so contextual rules simply don't match there.
  static constexpr size_t MAX_CSS_ANCESTORS = 16;
  std::array<CssAncestor, MAX_CSS_ANCESTORS> cssAncestors{};
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveTextAlignDefined = false;
  CssTextAlign effectiveTextAlign = CssTextAlign::Left;
  bool effectiveSup = false;
  bool effectiveSub = false;
  bool effectiveSmallCaps = false;
  bool effectivePreserveWhitespace = false;
  bool pendingPageBreak = false;  // set when a page-break-after element closes

  // Initial letter captured from a ::first-letter block or a leading float/initial-letter span,
  // drawn as a PageDropCap when its block is laid out.
  static constexpr size_t MAX_DROP_CAP_BYTES = 12;
  struct DropCapState {
    char text[MAX_DROP_CAP_BYTES + 1] = {};
    uint8_t length = 0;
    uint8_t codepoints = 0;
    uint8_t lines = 0;  // text lines the letter spans
    bool bold = false;
    bool firstLetterPending = false;  // capture the current block's first letter
    int spanDepth = -1;               // capturing the text of the span at this depth
  } dropCap;

  // Open elements with a CSS border or background shade. Each emits a PageBorderBox around
  // its content on every page it spans.
  struct BoxScope {
    int depth = 0;
    int16_t left = 0;
    int16_t right = 0;
    int16_t padTop = 0;  // outer edge to content, border included
    int16_t padBottom = 0;
    CssBorderSide sides[4];  // top, right, bottom, left
    bool shaded = false;
    int16_t top = -1;  // content extent on the current page
    int16_t bottom = -1;
    bool continued = false;  // sliced onto an earlier page
  };
  static constexpr size_t MAX_BOX_SCOPES = 8;

  // Page-break carry-over: a paragraph's orphan/widow lines and a heading kept with what
  // follows move to the next page instead of being left behind.
  static constexpr size_t MAX_CARRIED_LINES = 6;
  const ParsedText* layoutParagraph = nullptr;
  bool layoutParagraphHasDropCap = false;
  uint8_t paragraphLinesOnPage = 0;
  uint8_t keepWithNextLines = 0;                       // trailing heading lines on this page
  uint32_t recentLineOffsets[MAX_CARRIED_LINES] = {};  // visible offsets of this page's last lines, newest last
  uint8_t recentLineCount = 0;
  size_t linesToCarry(size_t& paragraphLines) const;
  size_t keepWithNextCarry() const;
  bool canCarry(size_t carry) const;
  void breakPageCarryingLines(size_t carry, size_t paragraphLines, uint32_t visibleOffset);
  std::vector<BoxScope> boxScopes;
  static constexpr size_t MAX_GRID_TABLE_COLUMNS = 4;
  // ponytail: buffer at most 160 tokens / 2 KB per row; larger rows use stacked flow.
  static constexpr size_t MAX_GRID_TABLE_ROW_WORDS = 160;
  static constexpr size_t MAX_GRID_TABLE_ROW_BYTES = 2048;
  int tableDepth = 0;
  bool insideTableCell = false;
  bool tableRowStacked = false;
  // A cell that spans rows cannot be drawn in a grid, and the span keeps applying to the
  // rows below it, so the whole run of affected rows falls back to stacked. UINT16_MAX
  // means "to the end of the table" (rowspan=0 in HTML).
  uint16_t tableRowsSpannedRemaining = 0;
  size_t tableRowTextBytes = 0;
  std::vector<std::unique_ptr<ParsedText>> tableRowCells;
  // Per-row scratch reused across rows: clear() keeps the capacity, so a long table
  // allocates these once instead of once per row.
  std::array<std::vector<std::unique_ptr<TextBlock>>, MAX_GRID_TABLE_COLUMNS> tableCellLines;
  std::vector<uint32_t> tableLineVisibleOffsets;
  bool listItemBulletOnly = false;  // true when currentTextBlock has only the <li> bullet
  CssBorderSide tableBorder;        // grid lines for the current table; invisible when unbordered
  // Column widths planned from a pre-scan of the table's markup; 0 columns = equal widths.
  std::array<uint16_t, MAX_GRID_TABLE_COLUMNS> tableColumnWidths{};
  uint8_t tableColumnCount = 0;
  struct TableColumnMeasure {
    uint16_t minWidth[MAX_GRID_TABLE_COLUMNS] = {};   // longest word
    uint16_t prefWidth[MAX_GRID_TABLE_COLUMNS] = {};  // longest unwrapped line
    uint8_t columns = 0;
  };

  // A font-size span that opens a block and holds all of its text sizes the whole block,
  // e.g. <p><span class="big">Chapter One</span></p>. Text after the span cancels it.
  struct InlineSizeState {
    float scale = 1.0f;
    int depth = -1;  // outermost sizing span
    bool valid = false;
    bool open = false;
  } inlineSize;

  // Tracks the innermost open <ul>/<ol> so <li> knows whether to number itself,
  // bullet itself, or (list-style-type: none) emit no marker at all. Pushed on
  // <ul>/<ol> open, popped on close, so nested lists restart their own counter
  // without disturbing the parent list's.
  struct ListContext {
    bool ordered = false;
    bool styleNone = false;
    int counter = 0;
    int depth = 0;  // parser depth at open; a display:none list never pushes, so its close must not pop
  };
  std::vector<ListContext> listStack;

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on. Keys are
  // arxHash64 of the id, never the id text — see the note on arxHash64 above.
  int completedPageCount = 0;
  std::vector<std::pair<uint64_t, uint16_t>> anchorData;
  // Deferred until after the previous text block is flushed. Optional rather than a
  // sentinel value: 0 is a legal hash, so "no anchor pending" needs its own state.
  std::optional<uint64_t> pendingAnchorId;
  std::vector<uint64_t> tocAnchors;  // the anchors that are TOC chapter boundaries
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;
  // Canonical reading-position counter: zero-based Unicode codepoints in visible
  // <body> text. Token offsets flow through line breaking so every completed page
  // records the first source character it renders.
  uint32_t visibleTextOffset = 0;
  uint32_t partWordVisibleOffset = 0;
  uint32_t currentPageVisibleOffset = 0;
  bool currentPageVisibleOffsetSet = false;
  bool insideBody = false;
  bool htmlEnded_ = false;
  bool syntheticCharacterData = false;
  uint16_t nonVisibleTextDepth = 0;

  // Paragraph-numbers feature: a per-chapter ordinal counting VISIBLE paragraphs
  // (blocks that emit >=1 line), used to tag the first line of each paragraph.
  // Fresh per section build (the parser is constructed once per build), so it
  // resets to 0 at each chapter — the reader adds the whole-book base at render.
  uint16_t paragraphOrdinal_ = 0;
  bool pendingParagraphFirstLine_ = false;  // set at makePages(), consumed by the block's first line
  // Depth watermark for h1-h6, matching boldUntilDepth: set when a heading opens, released
  // when that same depth closes. Blocks record their own heading-ness at creation from it,
  // because a block is laid out only when the NEXT element opens, long after this has moved on.
  int headingUntilDepth_ = INT_MAX;
  bool insideHeading() const { return headingUntilDepth_ < depth; }

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  // Resumable parse state. The one-shot parseAndBuildPages() drives these
  // internally; the incremental section builder drives them across render ticks
  // so a large single chapter can yield between pages instead of blocking the UI
  // until the whole thing is laid out. parseFile_ and the expat parser stay alive
  // for the lifetime of the parse so it can be paused and resumed at buffer
  // boundaries.
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  // Bytes of a tag that straddled the last chunk boundary, waiting to be judged with the
  // next chunk in front of them. Fixed size and part of the parser object: no allocation,
  // and the parser is already heap-held for the length of a build.
  char carry_[VoidTagFixer::MAX_CARRY] = {};
  size_t carryLen_ = 0;
  uint32_t parseStartTime_ = 0;

  void updateEffectiveInlineStyle();
  void startNewTextBlock(const BlockStyle& blockStyle, bool paragraphEnd = true);
  // Close the current page if it holds anything; false only on OOM for the next one.
  bool breakPage();
  void breakBeforeHeading();
  void flushPendingAnchor();
  void completeCurrentPage();
  void emitCurrentPage();
  void noteContent(int top, int bottom);
  void openBoxScope(BlockStyle& ownStyle, const CssStyle& cssStyle);
  void closeBoxScope();
  void emitBoxSegment(const BoxScope& box, bool closing);
  void armFirstLetterDropCap(const char* tagName, const std::string& classAttr, const char* idAttr);
  bool captureDropCapCodepoint(const char* bytes, int length);
  void cancelDropCapToWord();
  void layoutCurrentBlock(bool includeLastLine);
  void layoutDropCapLines(int layoutFontId, uint16_t effectiveWidth,
                          const std::function<void(std::unique_ptr<TextBlock>, uint32_t)>& emitLine,
                          bool includeLastLine);
  static uint8_t dropCapLines(const CssStyle& style);
  bool measureTableColumns(HalFile& file, TableColumnMeasure& out) const;
  void planTableColumns(const TableColumnMeasure& measure);
  void applyPendingPageBreak();
  void pushBlockStyle(const BlockStyle& accumulated);
  void flushPartWordBuffer();
  void breakTextLine(const BlockStyle& style);
  void fallbackTableRowToStacked();
  void closeTableCell();
  void finishTableRow();
  void addTableRowSeparator();
  void setCurrentPageVisibleOffset(uint32_t offset);
  void makePages(bool includeLastLine = true, bool paragraphEnd = true);
  void applyBlockFontScale(BlockStyle& blockStyle, const CssStyle& cssStyle, const char* tagName) const;
  int fontIdForScale(float scale) const;
  int prepareBlockFont(ParsedText& text, float scale);
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyInlinePresentationToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushBlockTextStyleEntry(const CssStyle& cssStyle);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  // Takes the render settings as the spec they already live in: listing them one by one
  // put two adjacent uint8_t (paragraphSpacing, wordSpacing) where a swap compiled.
  explicit ChapterHtmlSlimParser(Epub* epub, const std::string& filepath, GfxRenderer& renderer,
                                 const ReaderRenderSpec& spec, const PageCompleteFn completePageFn,
                                 void* const completePageCtx, const std::string& contentBase,
                                 const std::string& imageBasePath, std::vector<uint64_t> tocAnchors = {},
                                 const PopupFn popupFn = nullptr, void* const popupCtx = nullptr,
                                 const CssParser* cssParser = nullptr)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(spec.fontId),
        lineCompression(spec.lineCompression),
        extraParagraphSpacing(spec.extraParagraphSpacing),
        wordSpacing(spec.wordSpacing),
        paragraphSpacing(spec.paragraphSpacing),
        paragraphAlignment(spec.paragraphAlignment),
        viewportWidth(spec.viewportWidth),
        viewportHeight(spec.viewportHeight),
        focusReadingEnabled(spec.focusReadingEnabled),
        guideDotsMode(spec.guideDotsMode),
        firstLineIndentMode(spec.firstLineIndentMode),
        firstLineIndentPercent(spec.firstLineIndentPercent),
        linkUnderline(spec.linkUnderline),
        completePageFn(completePageFn),
        completePageCtx(completePageCtx),
        popupFn(popupFn),
        popupCtx(popupCtx),
        cssParser(cssParser),
        embeddedTextStyle(spec.embeddedTextStyle),
        embeddedLayoutStyle(spec.embeddedLayoutStyle),
        bookMargins(spec.bookMargins),
        wordExpansion(spec.wordExpansion),
        headingPageBreak(spec.headingPageBreak),
        imageRendering(spec.imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {}

  ~ChapterHtmlSlimParser();

  // One-shot parse: builds every page before returning (begin + step* + finish).
  bool parseAndBuildPages();

  // Resumable parse, for the incremental section builder. Drive as:
  //   if (!beginParse()) fail;
  //   loop: switch (parseStep()) { More: keep going / yield; Done: finishParse(); Error: abortParse(); }
  // Pages are emitted via completePageFn as they complete during parseStep(), so
  // the caller can stop once enough pages are built and resume on a later tick.
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();  // flush the trailing page and tear down; returns true
  void abortParse();   // tear down without flushing (error / abandon)

  void addLineToPage(std::unique_ptr<TextBlock> line, uint32_t visibleOffset);
  const std::vector<std::pair<uint64_t, uint16_t>>& getAnchors() const { return anchorData; }

  // Byte progress of the in-flight parse, used to estimate a still-building section's total page
  // count (a giant single-spine book never fully lays out, so its real count is unknown). Valid
  // between beginParse() and finishParse()/abortParse().
  size_t parseBytesConsumed() { return parseFile_ ? parseFile_.position() : 0; }
  size_t parseTotalBytes() { return parseFile_ ? parseFile_.size() : 0; }
};
