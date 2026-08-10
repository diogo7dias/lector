#pragma once

#include <Txt.h>

#include <vector>

#include "CrossPointSettings.h"
#include "ReaderUtils.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/SdStatsFiles.h"

class TxtReaderActivity final : public Activity {
  // Reading statistics. This reader has only the small Confirm popup below, not the
  // EPUB reader's tabbed menu, so the Reading Stats screen is still reached from the
  // EPUB reader. Time and pages read here still land in the all-books totals.
  reading_stats::SdStatsFiles statsFiles;
  reading_stats::ReaderStatsSession statsSession{statsFiles};
  bool statsTrackingActive = false;
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<std::string> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;

  // Pairs the Back release with the press this activity saw, so a release left over by a
  // child screen that closed on press is not read as "leave the book".
  ReaderUtils::ButtonPressLatch backLatch_;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  // Confirm inside a TXT book opens this small popup instead of a full menu: a plain
  // text file has only a handful of things worth changing, so three rows cover it.
  OptionPopup settingsPopup;
  // Set when the popup's Delete row has been confirmed; the file and its cache are
  // removed in onExit, after the Txt handle is released, the same ordering the EPUB
  // reader uses for its move-on-exit filing.
  bool pendingDeleteBook = false;

  void openSettingsPopup();
  void openFontPopup();
  void openSizePopup();
  void askDeleteBook();
  // Re-layout after a font or size change: rebuilds the page index against the new
  // font and lands on the page holding the byte the reader was showing before.
  void relayoutForFontChange();

  void renderPage();
  void renderStatusBar() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<std::string>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void savePageIndexCache() const;
  void saveProgress() const;
  void loadProgress();

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             int initialRefreshCountdown)
      : Activity("TxtReader", renderer, mappedInput),
        txt(std::move(txt)),
        pagesUntilFullRefresh(initialRefreshCountdown) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool appliesNightMode() const override { return true; }
  bool handleForcedRefresh() override {
    {
      RenderLock lock(*this);
      pagesUntilFullRefresh = 0;
    }
    requestUpdate();
    return true;
  }
  ScreenshotInfo getScreenshotInfo() const override;
};
