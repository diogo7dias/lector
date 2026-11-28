#pragma once

#include <Txt.h>

#include <vector>

#include "CrossPointSettings.h"
#include "EndOfBookOptions.h"
#include "ReaderPrefs.h"
#include "ReaderProgressSaveDebouncer.h"
#include "ReaderUtils.h"
#include "SimpleReaderShortcut.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class TxtReaderActivity final : public Activity {
  std::unique_ptr<Txt> txt;

  int currentPage = 0;
  int totalPages = 1;
  int pagesUntilFullRefresh = 0;

  struct TxtLine {
    std::string text;
    bool isParagraphEnd = true;
  };

  // Streaming text reader - stores file offsets for each page
  std::vector<size_t> pageOffsets;  // File offset for start of each page
  std::vector<TxtLine> currentPageLines;
  int linesPerPage = 0;
  int viewportWidth = 0;
  bool initialized = false;
  // Set before a relayout from the light panel: the next index build skips its Indexing popup.
  bool quietIndex_ = false;
  bool unsupportedEncoding = false;  // UTF-16 file: nothing indexed, a message instead
  bool legacy8Bit = false;           // Windows-1252 / Latin-1 bytes, decoded to UTF-8 per page

  // Per-book look (loaded from <cachePath>/reader_override.bin, or global default).
  ReaderPrefs prefs_;
  bool prefsCustom_ = false;
  ReaderPrefs readerEdit_;

  // Pairs the Back release with the press this activity saw, so a release left over by a
  // child screen that closed on press is not read as "leave the book".
  ReaderUtils::ButtonPressLatch backLatch_;

  // Cached settings for cache validation (different fonts/margins require re-indexing)
  int cachedFontId = 0;
  float cachedLineCompression = 1.0f;
  int8_t cachedCharacterSpacing = 0;
  uint8_t cachedScreenMargin = 0;
  uint8_t cachedParagraphAlignment = CrossPointSettings::LEFT_ALIGN;
  int cachedOrientedMarginTop = 0;
  int cachedOrientedMarginRight = 0;
  int cachedOrientedMarginBottom = 0;
  int cachedOrientedMarginLeft = 0;

  // Confirm inside a TXT book opens the book popup (Text Settings, Reading Themes, Reset, Delete).
  OptionPopup settingsPopup;
  // Set when the popup's Delete row has been confirmed; the file and its cache are
  // removed in onExit, after the Txt handle is released, the same ordering the EPUB
  // reader uses for its move-on-exit filing.
  bool pendingDeleteBook = false;
  // Status bar only, this sitting: forward turns ("+N").
  int sessionPages = 0;
  // Pages-to-paragraph answer for page paraPagesFor (-1 = none), kept so a redraw of
  // the same page does not re-read the file. Reset whenever the page index changes.
  mutable int paraPagesFor = -1;
  mutable int paraPagesLeft = -1;

  std::string readerOverridePath() const;
  bool writeReaderOverride(const ReaderPrefs& p) const;
  void loadReaderPrefs();
  static void readerEditSinkThunk(void* ctx, const ReaderPrefs& live);
  void persistReaderSettingsEdit(const ReaderPrefs& live) const;
  void applyReaderSettingsEdit();
  ReaderPrefs applyReaderPrefsFrom(const ReaderPrefs& incoming);
  void resetReaderPrefsToGlobal();
  void openSettingsPopup();
  void openReaderSettings();
  void openReadingThemes();
  void askResetReaderSettings();
  void askDeleteBook();
  // Re-layout after a font or look change: rebuilds the page index against the new
  // look and lands on the page holding the byte the reader was showing before.
  void relayoutForFontChange();

  void renderPage();
  // One page forward or back. Shared
  // by the side keys and by a paging action bound to any other button.
  void pageTurn(bool forward);
  void returnToLastPage();
  void skipPages(int delta);
  void renderStatusBar() const;
  // Pages after the current one wholly inside the paragraph running off its bottom
  // (">P.N"), as the EPUB reader counts them. -1 when the file cannot be read.
  int pagesUntilNextParagraph() const;

  void initializeReader();
  bool loadPageAtOffset(size_t offset, std::vector<TxtLine>& outLines, size_t& nextOffset);
  void buildPageIndex();
  bool loadPageIndexCache();
  void detectEncoding();
  void savePageIndexCache() const;
  void saveProgress(uint32_t page);
  void loadProgress();
  ReaderProgressSaveDebouncer progressSaveDebouncer;
  EndOfBookOptions endOfBookOptions;

 public:
  explicit TxtReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                             int initialRefreshCountdown)
      : Activity(activity_name::kTxtReader, renderer, mappedInput),
        txt(std::move(txt)),
        pagesUntilFullRefresh(initialRefreshCountdown) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  // This reader has no in-book menu, so most bound functions cannot run here. The
  // detector is armed only for the ones that can (see SimpleReaderShortcut.h), so a
  // binding this reader cannot honour does not cost every power click the ~280 ms
  // hold-back the detector needs.
  bool isBookContext() const override { return true; }
  void onCalmModeChanged() override {
    quietIndex_ = true;
    relayoutForFontChange();
  }
  bool runBoundAction(uint8_t function) override;
  bool appliesNightMode() const override { return true; }
  void onRevealed() override { pagesUntilFullRefresh = 0; }
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
