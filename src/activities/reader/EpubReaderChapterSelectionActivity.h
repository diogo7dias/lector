#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "activities/UiListActivity.h"

class EpubReaderChapterSelectionActivity final : public UiListActivity {
 public:
  explicit EpubReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                              const std::shared_ptr<Epub>& epub, const std::string& epubPath,
                                              const int currentSpineIndex)
      : UiListActivity("EpubReaderChapterSelection", renderer, mappedInput),
        epub(epub),
        epubPath(epubPath),
        currentSpineIndex(currentSpineIndex) {}

  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onBackButton() override;
  const char* headerTitle() const override;

 private:
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  int currentSpineIndex = 0;

  // Visible-row cache. TOC entries are SD-backed (BookMetadataCache LUT reads) and the
  // list redraws every visible row on each repaint, so a CJK table of contents used to
  // pay one fallback-glyph SD pass per row per repaint. Materializing just the drawn
  // page and batch-prewarming its glyphs turns that into one bounded pass per page;
  // repaints inside the page stay in RAM (upstream #3071). The list reads the same
  // window through ListProps::itemsWindowFirst, so nothing outside it is ever touched.
  static constexpr int TOC_WINDOW = 24;
  std::string windowLabels[TOC_WINDOW];
  std::vector<freeink::ui::ListItem> rows;
  int windowStart = -1;
  int windowCount = 0;
  void refreshTocWindow(int start, int count);
  // Indent + title for one TOC entry, as drawn.
  std::string tocLabelAt(int index) const;
};
