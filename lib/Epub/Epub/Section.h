#pragma once
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Epub.h"
#include "LayoutParams.h"

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
  void selectGeneration(const LayoutParams& lp);

  // State of an in-progress incremental build. Non-null only while building; the
  // parser and HTML file stay live between buildSomeMore() calls so layout can be
  // driven a few pages at a time. Declared with tmpHtmlPath before parser so the
  // parser (which holds a reference to it) is destroyed first.
  struct BuildContext {
    std::string tmpHtmlPath;  // unzipped chapter HTML; referenced by parser, removed at finalize
    std::string partPath;     // "<section>.bin.part" being written; renamed over filePath on commit
    // Page LUT as a nothrow-grown flat array (a std::vector would abort() on a
    // failed growth under -fno-exceptions); see ensurePageLutCapacity.
    std::unique_ptr<PageLutEntry[]> lut;
    uint16_t lutCapacity = 0;
    uint16_t lutCount = 0;
    bool lutFailed = false;          // LUT could not grow; buildSomeMore abandons the build
    CssParser* cssParser = nullptr;  // cleared at finalize/abandon
    std::unique_ptr<ChapterHtmlSlimParser> parser;
  };
  std::unique_ptr<BuildContext> build_;
  bool buildComplete_ = false;
  uint16_t builtPageCount_ = 0;  // pages laid out by the active build (== build_->lutCount)
  // True when the most recent build failed specifically because the heap ran
  // critically low during layout (as opposed to a parse/IO error). Captured from
  // the parser before the failed build is torn down; read via lastBuildWasLowMemory().
  bool lastBuildLowMemory_ = false;
  // Cooperative cancel. buildSomeMore() polls buildCancelRequested_ between parse
  // steps and, when set, abandons the in-progress build so a foreground caller
  // (the reader) reclaims the render lock in ~one parse step instead of blocking
  // for the whole ~13s layout. volatile: it is set from the UI task while the
  // render task runs the build (single core, so a plain flag is sufficient; the
  // context switch is the barrier, volatile just stops the compiler caching it).
  // lastBuildCancelled_ lets the caller tell a user-cancel apart from an error so
  // it can bail quietly (no build-error screen).
  volatile bool buildCancelRequested_ = false;
  bool lastBuildCancelled_ = false;

  // Returns false on a short write (SD full / IO error) so the caller can abandon
  // the .part instead of building on a truncated header.
  bool writeSectionFileHeader(const LayoutParams& lp);
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
  // Total numbered paragraphs (headings excluded) in this chapter, captured from
  // the parser at build finalize. 0 until this section has been built this
  // session (a cache load does not set it). Feeds the go-to-paragraph picker and
  // whole-book paragraph numbering.
  uint16_t paragraphCount = 0;

  Section(const std::shared_ptr<Epub>& epub, int spineIndex, GfxRenderer& renderer);
  ~Section();
  // Side-effect-free probe: true if a cached section file exists for these
  // exact layout parameters. Unlike loadSectionFile it never switches or
  // prunes cache generations, so it is safe to call for several candidate
  // parameter sets in a row.
  bool hasCachedSectionFor(const LayoutParams& lp) const;

  bool loadSectionFile(const LayoutParams& lp);
  bool clearCache() const;
  // One-shot full build. Thin wrapper over startBuild() + buildSomeMore(0), retained
  // so existing callers are unaffected.
  bool createSectionFile(const LayoutParams& lp, const std::function<void()>& popupFn = nullptr);

  // Incremental build API. startBuild() streams the chapter HTML, opens the .part and
  // primes the parser without laying out any pages. buildSomeMore(maxPages) lays out
  // up to maxPages more pages (<= 0 = to completion), returning true while the build
  // is still viable; on reaching the end it flushes, finalizes and commits the cache.
  // A build is torn down (persist-less) by abandonBuild() and by the destructor.
  bool startBuild(const LayoutParams& lp, const std::function<void()>& popupFn = nullptr);
  bool buildSomeMore(int maxPages);
  bool isBuilding() const { return static_cast<bool>(build_); }
  bool isBuildComplete() const { return buildComplete_; }
  // True if the last createSectionFile()/buildSomeMore() failure was a low-memory
  // abort during layout. Lets the caller degrade render quality and retry rather
  // than treat it as a hard error. Valid after a build call returns false.
  bool lastBuildWasLowMemory() const { return lastBuildLowMemory_; }
  // Ask the active build to stop at the next parse step. Safe to call from another
  // task (only writes a flag); a no-op if no build is in flight. The flag is
  // cleared at the start of the next startBuild(), so it only affects the build
  // that was running when it was set.
  void requestBuildCancel() { buildCancelRequested_ = true; }
  // True if the last build stopped because requestBuildCancel() was honored, as
  // opposed to completing or failing. Valid after a build call returns false.
  bool lastBuildWasCancelled() const { return lastBuildCancelled_; }
  void abandonBuild();
  // Best-known total page count: the exact pageCount once finalized, or a byte-ratio
  // extrapolation while a build is still in flight.
  uint16_t estimatedTotalPages() const;
  // Pages laid out so far by the active build (0 before startBuild). Numerator for
  // a build progress bar; estimatedTotalPages() is the denominator.
  uint16_t builtPages() const { return builtPageCount_; }

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
