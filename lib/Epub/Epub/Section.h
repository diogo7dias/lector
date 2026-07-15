#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Epub.h"

class Page;
class GfxRenderer;
class CssParser;
class ChapterHtmlSlimParser;

class Section {
 public:
  // One entry per laid-out page, accumulated in RAM during a build: byte offset of
  // the serialized page in the section file, plus the synthetic paragraph / list-item
  // index used for XPath and progress mapping.
  struct PageLutEntry {
    uint32_t fileOffset;
    uint16_t paragraphIndex;
    uint16_t listItemIndex;
  };

 private:
  std::shared_ptr<Epub> epub;
  const int spineIndex;
  GfxRenderer& renderer;
  std::string filePath;
  // Generation directory holding this section's cache + its image files:
  // "<cachePath>/sections/<hash8>", where hash8 covers every layout-affecting
  // render setting. Distinct settings get distinct directories, so toggling a
  // setting back reuses the previously built pages instead of re-indexing.
  // Set by selectGeneration(); the newest two generations are kept on disk.
  std::string sectionDirPath;
  HalFile file;

  // Derive the generation from the layout settings and point filePath at it.
  // Also prepares the directory and prunes stale generations (once per
  // generation per session).
  void selectGeneration(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                        uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                        uint8_t imageRendering, bool focusReadingEnabled, bool guideDotsEnabled, int firstLineIndentPx,
                        uint8_t wordSpacing, uint8_t paragraphSpacing);

  // State of an in-progress incremental build. Non-null only while building; the
  // parser and HTML file stay live between buildSomeMore() calls so layout can be
  // driven a few pages at a time. Declared with tmpHtmlPath before parser so the
  // parser (which holds a reference to it) is destroyed first.
  struct BuildContext {
    std::string tmpHtmlPath;  // unzipped chapter HTML; referenced by parser, removed at finalize
    std::string partPath;     // "<section>.bin.part" being written; renamed over filePath on commit
    std::vector<PageLutEntry> lut;
    CssParser* cssParser = nullptr;  // cleared at finalize/abandon
    std::unique_ptr<ChapterHtmlSlimParser> parser;
  };
  std::unique_ptr<BuildContext> build_;
  bool buildComplete_ = false;
  uint16_t builtPageCount_ = 0;  // pages laid out by the active build (== build_->lut.size())
  // True when the most recent build failed specifically because the heap ran
  // critically low during layout (as opposed to a parse/IO error). Captured from
  // the parser before the failed build is torn down; read via lastBuildWasLowMemory().
  bool lastBuildLowMemory_ = false;

  void writeSectionFileHeader(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                              uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled,
                              bool embeddedStyle, uint8_t imageRendering, bool focusReadingEnabled,
                              bool guideDotsEnabled, int firstLineIndentPx, uint8_t wordSpacing,
                              uint8_t paragraphSpacing);
  uint32_t onPageComplete(std::unique_ptr<Page> page);
  // Writes the LUT + trailers, patches the header, stamps the real version byte and
  // atomically renames the .part into place. Consumes build_. Returns true on commit.
  bool finalizeBuild();

 public:
  // Wake-diagnostics introspection: the cache file and generation dir this
  // section resolved to (valid after loadSectionFile/startBuild ran
  // selectGeneration).
  const std::string& cacheFilePath() const { return filePath; }
  const std::string& cacheGenerationDir() const { return sectionDirPath; }

  uint16_t pageCount = 0;
  int currentPage = 0;

  Section(const std::shared_ptr<Epub>& epub, int spineIndex, GfxRenderer& renderer);
  ~Section();
  bool loadSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                       uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                       uint8_t imageRendering, bool focusReadingEnabled, bool guideDotsEnabled, int firstLineIndentPx,
                       uint8_t wordSpacing, uint8_t paragraphSpacing);
  bool clearCache() const;
  // One-shot full build. Thin wrapper over startBuild() + buildSomeMore(0), retained
  // so existing callers are unaffected.
  bool createSectionFile(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                         uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                         uint8_t imageRendering, bool focusReadingEnabled, bool guideDotsEnabled, int firstLineIndentPx,
                         uint8_t wordSpacing, uint8_t paragraphSpacing, const std::function<void()>& popupFn = nullptr);

  // Incremental build API. startBuild() streams the chapter HTML, opens the .part and
  // primes the parser without laying out any pages. buildSomeMore(maxPages) lays out
  // up to maxPages more pages (<= 0 = to completion), returning true while the build
  // is still viable; on reaching the end it flushes, finalizes and commits the cache.
  // A build is torn down (persist-less) by abandonBuild() and by the destructor.
  bool startBuild(int fontId, float lineCompression, bool extraParagraphSpacing, uint8_t paragraphAlignment,
                  uint16_t viewportWidth, uint16_t viewportHeight, bool hyphenationEnabled, bool embeddedStyle,
                  uint8_t imageRendering, bool focusReadingEnabled, bool guideDotsEnabled, int firstLineIndentPx,
                  uint8_t wordSpacing, uint8_t paragraphSpacing, const std::function<void()>& popupFn = nullptr);
  bool buildSomeMore(int maxPages);
  bool isBuilding() const { return static_cast<bool>(build_); }
  bool isBuildComplete() const { return buildComplete_; }
  // True if the last createSectionFile()/buildSomeMore() failure was a low-memory
  // abort during layout. Lets the caller degrade render quality and retry rather
  // than treat it as a hard error. Valid after a build call returns false.
  bool lastBuildWasLowMemory() const { return lastBuildLowMemory_; }
  void abandonBuild();
  // Best-known total page count: the exact pageCount once finalized, or a byte-ratio
  // extrapolation while a build is still in flight.
  uint16_t estimatedTotalPages() const;

  // Load a specific page: from the active build's .part via its in-RAM LUT if that
  // page is already laid out, else from the committed section file.
  std::unique_ptr<Page> loadPage(int page);
  std::unique_ptr<Page> loadPageFromSectionFile();
  std::string getTextFromSectionFile();

  // Look up the page number for an anchor id from the section cache file.
  std::optional<uint16_t> getPageForAnchor(const std::string& anchor) const;

  // Get the page count from the section cache file without fully loading it.
  std::optional<uint16_t> getCachedPageCount() const;

  // Look up the page number for a synthetic paragraph index from XPath p[N].
  std::optional<uint16_t> getPageForParagraphIndex(uint16_t pIndex) const;

  // Look up the page number for a running list-item index from the li LUT.
  std::optional<uint16_t> getPageForListItemIndex(uint16_t liIndex) const;

  // Look up the synthetic paragraph index for the given rendered page.
  std::optional<uint16_t> getParagraphIndexForPage(uint16_t page) const;
};
