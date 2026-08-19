#pragma once
#include <Epub.h>

#include <memory>
#include <string>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class EpubReaderChapterSelectionActivity final : public Activity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  ButtonNavigator buttonNavigator;
  int currentSpineIndex = 0;
  int selectorIndex = 0;

  // Visible-row cache. TOC entries are SD-backed (BookMetadataCache LUT reads) and the
  // list redraws every visible row on each repaint, so a CJK table of contents used to
  // pay one fallback-glyph SD pass per row per repaint. Materializing just the drawn
  // page and batch-prewarming its glyphs turns that into one bounded pass per page;
  // repaints inside the page stay in RAM (upstream #3071).
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  int windowStart = -1;
  int windowCount = 0;
  // start must be the same whole-page boundary BaseTheme::drawList() snaps paging
  // callers to, or the cache misses the rows actually drawn.
  void refreshTocWindow(int start, int pageItems);
  // Indent + title for one TOC entry, as drawn.
  std::string tocLabelAt(int index) const;

  // Number of items that fit on a page, derived from logical screen height.
  // This adapts automatically when switching between portrait and landscape.
  int getPageItems() const;

  // Total TOC items count
  int getTotalItems() const;

 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex)
      : Activity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
