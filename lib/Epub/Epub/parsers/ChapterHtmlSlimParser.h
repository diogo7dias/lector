#pragma once

#include <HalStorage.h>
#include <expat.h>

#include <climits>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Epub/FootnoteEntry.h"
#include "Epub/LayoutParams.h"
#include "Epub/ParsedText.h"
#include "Epub/blocks/ImageBlock.h"
#include "Epub/blocks/TextBlock.h"
#include "Epub/css/CssParser.h"
#include "Epub/css/CssStyle.h"

class Page;
class GfxRenderer;
class Epub;

#define MAX_WORD_SIZE 200

class ChapterHtmlSlimParser {
  std::shared_ptr<Epub> epub;
  const std::string& filepath;
  GfxRenderer& renderer;
  std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t)> completePageFn;
  std::function<void()> popupFn;  // Popup callback
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
  std::unique_ptr<Page> currentPage = nullptr;
  int16_t currentPageNextY = 0;
  int fontId;
  float lineCompression;
  bool extraParagraphSpacing;
  uint8_t paragraphAlignment;
  uint16_t viewportWidth;
  uint16_t viewportHeight;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  bool guideDotsEnabled;
  int firstLineIndentPx;     // -1 = book/CSS indent; >= 0 = explicit first-line indent px
  uint8_t wordSpacing;       // 10%-step, 3 = 0% (range 0..33 -> -30%..+300% of the space width)
  uint8_t paragraphSpacing;  // 10%-step, 0 = 0% (range 0..15 -> 0..150% of the line height, block gap)
  const CssParser* cssParser;
  bool embeddedStyle;
  uint8_t imageRendering;
  std::string contentBase;
  std::string imageBasePath;
  int imageCounter = 0;

  // Style tracking (replaces depth-based approach)
  struct StyleStackEntry {
    int depth = 0;
    bool hasBold = false, bold = false;
    bool hasItalic = false, italic = false;
    bool hasTextDecoration = false;
    CssTextDecoration textDecoration = CssTextDecoration::None;
    bool hasDirection = false;
    CssTextDirection direction = CssTextDirection::Ltr;
    bool hasSup = false, sup = false;
    bool hasSub = false, sub = false;
  };
  std::vector<StyleStackEntry> inlineStyleStack;
  std::vector<BlockStyle> blockStyleStack;  // accumulated block styles from open ancestor elements
  CssStyle currentCssStyle;
  bool effectiveBold = false;
  bool effectiveItalic = false;
  CssTextDecoration effectiveTextDecoration = CssTextDecoration::None;
  bool effectiveDirectionDefined = false;
  CssTextDirection effectiveDirection = CssTextDirection::Ltr;
  bool effectiveSup = false;
  bool effectiveSub = false;
  int tableDepth = 0;
  int tableRowIndex = 0;
  int tableColIndex = 0;
  bool listItemBulletOnly = false;  // true when currentTextBlock holds only the <li> bullet (upstream #2589)

  // Anchor-to-page mapping: tracks which page each HTML id attribute lands on
  int completedPageCount = 0;
  std::vector<std::pair<std::string, uint16_t>> anchorData;
  std::string pendingAnchorId;          // deferred until after previous text block is flushed
  std::vector<std::string> tocAnchors;  // the list of anchors that are TOC chapter boundaries
  uint16_t xpathParagraphIndex = 0;
  uint16_t xpathListItemIndex = 0;

  // Paragraph-number marks: a running 1-based paragraph counter for this chapter,
  // and a latch set when a block starts so only its FIRST emitted line is tagged
  // with the ordinal (see makePages + addLineToPage). Per-parser => per-chapter,
  // so counting resets each chapter (whole-book mode adds a chapter base offset
  // in the reader). Empty blocks emit no line, so they consume no number (no gaps).
  uint16_t paragraphOrdinal_ = 0;
  bool nextLineStartsParagraph_ = false;
  // True while the current block is a heading (h1-h6). Headings are titles, not
  // paragraphs, so they are NOT numbered. Set when a header tag opens, checked
  // and cleared in makePages (per-block flush).
  bool currentBlockIsHeading_ = false;

  // Footnote link tracking
  bool insideFootnoteLink = false;
  int footnoteLinkDepth = -1;
  FootnoteEntry currentFootnote = {};
  int currentFootnoteLinkTextLen = 0;
  std::vector<std::pair<int, FootnoteEntry>> pendingFootnotes;  // <wordIndex, entry>
  int wordsExtractedInBlock = 0;

  // Incremental parse state (pause/resume seam). Between beginParse() and finishParse()/abortParse()
  // these hold the live expat parser and the open chapter HTML file so the parse can advance one
  // chunk at a time via parseStep() instead of running to completion in a single blocking loop.
  XML_Parser xmlParser_ = nullptr;
  HalFile parseFile_;
  uint32_t parseStartTime_ = 0;
  uint32_t parseTotalBytes_ = 0;  // parseFile_.size() captured at begin (size() is non-const)
  // Set by parseStep() when it stops the build because the heap is critically low
  // (see LayoutHeapGate.h). Lets the caller distinguish an out-of-memory abort from
  // other parse errors so it can degrade quality and retry rather than give up.
  bool lowMemoryAbort_ = false;
  // One-shot latch: parseStep() releases the SD-font resident caches at most
  // once per build before declaring a low-memory abort.
  bool attemptedFontCacheRelease_ = false;

  void updateEffectiveInlineStyle();
  // Allocate a fresh currentPage with the nothrow allocator (bare new would
  // abort() on OOM under -fno-exceptions). On failure flags a low-memory
  // abort, stops the parse and returns false so callers bail out.
  bool allocCurrentPage();
  void startNewTextBlock(const BlockStyle& blockStyle);
  void flushPendingAnchor();
  void flushPartWordBuffer();
  void makePages();
  static EpdFontFamily::Style fontStyleForTextDecoration(CssTextDecoration decoration);
  static void applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css);
  static void applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css);
  void pushDecorationStyleEntry(CssTextDecoration defaultDecoration, const CssStyle& cssStyle);
  void emitHorizontalRule(const BlockStyle& blockStyle);
  // XML callbacks
  static void XMLCALL startElement(void* userData, const XML_Char* name, const XML_Char** atts);
  static void XMLCALL characterData(void* userData, const XML_Char* s, int len);
  static void XMLCALL defaultHandlerExpand(void* userData, const XML_Char* s, int len);
  static void XMLCALL endElement(void* userData, const XML_Char* name);

 public:
  // Layout inputs arrive as one LayoutParams (the same value that keys the
  // section cache), so the construction site cannot silently drift out of order
  // relative to the cache key. Individual members are still kept and simply
  // copied out of `lp` here — the parser body is unchanged.
  explicit ChapterHtmlSlimParser(std::shared_ptr<Epub> epub, const std::string& filepath, GfxRenderer& renderer,
                                 const LayoutParams& lp,
                                 const std::function<void(std::unique_ptr<Page>, uint16_t, uint16_t)>& completePageFn,
                                 const std::string& contentBase, const std::string& imageBasePath,
                                 std::vector<std::string> tocAnchors = {},
                                 const std::function<void()>& popupFn = nullptr, const CssParser* cssParser = nullptr)

      : epub(epub),
        filepath(filepath),
        renderer(renderer),
        fontId(lp.fontId),
        lineCompression(lp.lineCompression),
        extraParagraphSpacing(lp.extraParagraphSpacing),
        paragraphAlignment(lp.paragraphAlignment),
        viewportWidth(lp.viewportWidth),
        viewportHeight(lp.viewportHeight),
        hyphenationEnabled(lp.hyphenationEnabled),
        focusReadingEnabled(lp.focusReadingEnabled),
        guideDotsEnabled(lp.guideDotsEnabled),
        firstLineIndentPx(lp.firstLineIndentPx),
        wordSpacing(lp.wordSpacing),
        paragraphSpacing(lp.paragraphSpacing),
        completePageFn(completePageFn),
        popupFn(popupFn),
        cssParser(cssParser),
        embeddedStyle(lp.embeddedStyle),
        imageRendering(lp.imageRendering),
        contentBase(contentBase),
        imageBasePath(imageBasePath),
        tocAnchors(std::move(tocAnchors)) {
    // Typical chapters carry a few dozen navigation anchors; reserving up front
    // avoids the vector's doubling reallocations during the layout build, when
    // the heap is at its most fragmented. Pathological span-per-paragraph books
    // (Kobo/KePub converters) still grow toward MAX_ANCHORS_PER_CHAPTER.
    anchorData.reserve(64);
  }

  ~ChapterHtmlSlimParser() { abortParse(); }

  // One-shot layout of the whole chapter. Retained as a thin wrapper over the
  // beginParse()/parseStep()/finishParse() seam below so existing callers are unaffected.
  bool parseAndBuildPages();

  // Pause/resume seam. beginParse() sets up expat + opens the HTML; parseStep() advances one
  // PARSE_BUFFER_SIZE chunk (emitting any completed pages via completePageFn) and reports whether
  // more remains; finishParse() flushes the trailing page and tears down; abortParse() tears down
  // without flushing (error/abandon path, also called by the destructor).
  enum class ParseStatus { More, Done, Error };
  bool beginParse();
  ParseStatus parseStep();
  bool finishParse();
  void abortParse();
  // Byte progress of the active parse (valid between beginParse() and finishParse()).
  size_t parseBytesConsumed() const { return parseFile_.position(); }
  size_t parseTotalBytes() const { return parseTotalBytes_; }
  // True when the most recent parseStep() returned Error specifically because the
  // heap was critically low, rather than for a parse/IO error.
  bool wasLowMemoryAbort() const { return lowMemoryAbort_; }

  void addLineToPage(std::shared_ptr<TextBlock> line);
  const std::vector<std::pair<std::string, uint16_t>>& getAnchors() const { return anchorData; }
};
