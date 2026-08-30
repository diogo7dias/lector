#pragma once
#include <OpdsParser.h>

#include <string>
#include <utility>
#include <vector>

#include "OpdsServerStore.h"
#include "activities/UiStatusActivity.h"

/**
 * Activity for browsing and downloading books from an OPDS server.
 * Supports navigation through catalog hierarchy and downloading EPUBs.
 */
class OpdsBookBrowserActivity final : public UiStatusActivity {
 public:
  enum class BrowserState { CHECK_WIFI, WIFI_SELECTION, LOADING, BROWSING, DOWNLOADING, ERROR, SEARCH_INPUT };

  explicit OpdsBookBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, OpdsServer server)
      : UiStatusActivity("OpdsBookBrowser", renderer, mappedInput), server(std::move(server)) {}

  void onEnter() override;
  void onExit() override;

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onListActivated(int index) override;
  void onBackButton() override;
  void onConfirmButton() override;
  void drawHeaderExtras(const Rect& headerRect) override;
  void afterRender() override;

 private:
  BrowserState state = BrowserState::LOADING;
  // The rows and the strings behind them: statusView() only hands out pointers,
  // so both outlive it. Rebuilt with every feed.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> rowLabels;
  void refreshRows();
  std::vector<OpdsEntry> entries;
  std::vector<std::string> navigationHistory;
  std::string currentPath;
  std::string searchTemplate;
  bool consumeConfirm = false;
  bool consumeBack = false;  // Added missing member
  /** The line the download screen names the book on. */
  std::string downloadTitle;
  // Spend one FULL refresh on the frame that replaces the download screen. That
  // screen repaints on every progress step, all in FAST, so it leaves the panel
  // heavily ghosted.
  bool pendingFullRefresh = false;
  std::string errorMessage;
  std::string statusMessage;
  size_t downloadProgress = 0;
  size_t downloadTotal = 0;

  OpdsServer server;  // Copied at construction — safe even if the store changes during browsing

  void checkAndConnectWifi();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
  void fetchFeed(const std::string& path);
  void releaseEntries();
  void navigateToEntry(const OpdsEntry& entry);
  void navigateBack();
  void downloadBook(const OpdsEntry& book);
  void launchSearch();
  void performSearch(const std::string& query);
  bool preventAutoSleep() override { return true; }
};
