#pragma once

#include <Epub/MarkdownSection.h>
#include <Txt.h>

#include <memory>
#include <string>

#include "CrossPointSettings.h"
#include "ReaderPrefs.h"
#include "ReaderProgressSaveDebouncer.h"
#include "ReaderUtils.h"
#include "SimpleReaderShortcut.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"

class MarkdownReaderActivity final : public Activity {
  std::unique_ptr<Txt> txt_;
  std::unique_ptr<MarkdownSection> section_;

  int currentPage_ = 0;
  int totalPages_ = 0;
  int pagesUntilFullRefresh_ = 0;
  int sessionPages_ = 0;
  bool initialized_ = false;
  bool quietBuild_ = false;
  bool pendingDeleteBook_ = false;

  ReaderPrefs prefs_;
  bool prefsCustom_ = false;
  ReaderPrefs readerEdit_;

  int marginTop_ = 0;
  int marginRight_ = 0;
  int marginBottom_ = 0;
  int marginLeft_ = 0;

  ReaderUtils::ButtonPressLatch backLatch_;
  OptionPopup menuPopup_;
  ReaderProgressSaveDebouncer progressSaveDebouncer_;

  std::string readerOverridePath() const;
  bool writeReaderOverride(const ReaderPrefs& p) const;
  void loadReaderPrefs();
  static void readerEditSinkThunk(void* ctx, const ReaderPrefs& live);
  void persistReaderSettingsEdit(const ReaderPrefs& live) const;
  void applyReaderSettingsEdit();
  ReaderPrefs applyReaderPrefsFrom(const ReaderPrefs& incoming);
  void resetReaderPrefsToGlobal();

  void initializeReader();
  void relayoutPreservingPercent();
  void renderPage();
  void renderStatusBar() const;
  std::string currentHeadingTitle() const;
  void pageTurn(bool forward);
  void jumpToAdjacentHeading(bool forward);
  void openMenuPopup();
  void openTocPopup();
  void openReaderSettings();
  void openReadingThemes();
  void askResetReaderSettings();
  void askDeleteBook();
  void saveProgress(uint32_t page);
  void loadProgress();

 public:
  explicit MarkdownReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Txt> txt,
                                  int initialRefreshCountdown)
      : Activity(activity_name::kMarkdownReader, renderer, mappedInput),
        txt_(std::move(txt)),
        pagesUntilFullRefresh_(initialRefreshCountdown) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool isReaderActivity() const override { return true; }
  bool isBookContext() const override { return true; }
  bool runBoundAction(uint8_t function) override;
  bool appliesNightMode() const override { return true; }
  void onRevealed() override { pagesUntilFullRefresh_ = 0; }
  bool handleForcedRefresh() override {
    {
      RenderLock lock(*this);
      pagesUntilFullRefresh_ = 0;
    }
    requestUpdate();
    return true;
  }
  ScreenshotInfo getScreenshotInfo() const override;
};
