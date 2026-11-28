#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>

#include <utility>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "OpdsBookBrowserActivity.h"
#include "components/icons/search24.h"
#include "util/OpdsFilename.h"

namespace {
constexpr int SEARCH_ICON_SIZE = 24;
constexpr int SEARCH_ICON_MARGIN = 14;
constexpr unsigned long GO_HOME_MS = 1000;  // Back held this long leaves for Home (upstream #4397)

// The search glyph sits inside the header band, at its right edge.
int searchIconX(const GfxRenderer& renderer) {
  return renderer.getScreenWidth() - SEARCH_ICON_SIZE - SEARCH_ICON_MARGIN;
}
}  // namespace

// --- Rows and screen ---

bool OpdsBookBrowserActivity::isBookOnCard(const OpdsEntry& entry) const {
  if (entry.type != OpdsEntryType::BOOK) return false;
  const std::string path =
      opdsTargetPath(SETTINGS.opdsDownloadFolder, entry.author, entry.title,
                     static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat), opdsBookExtension(entry.format));
  return Storage.exists(path.c_str());
}

void OpdsBookBrowserActivity::refreshRows() {
  // The rows borrow the feed's strings: a second copy of every title and author is heap
  // this device lacks with WiFi and TLS up.
  rows.resize(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const OpdsEntry& entry = entries[i];
    rows[i] = freeink::ui::ListItem{};
    rows[i].label = entry.title.c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
    // An arrow marks a place to go; a book carries its author under the title and an
    // "[On card]" tag when the target path is already present on the SD card.
    if (entry.type == OpdsEntryType::NAVIGATION) {
      rows[i].value = ">";
    } else {
      if (!entry.author.empty()) rows[i].subtitle = entry.author.c_str();
      if (isBookOnCard(entry)) rows[i].value = tr(STR_OPDS_ON_CARD);
    }
  }
}

UiStatusActivity::StatusView OpdsBookBrowserActivity::statusView() const {
  StatusView view;
  view.title = server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str();
  // A TLS loan returns the framebuffer white and downloads repaint FAST: owe a full pass.
  if (pendingFullRefresh) view.refresh = HalDisplay::FULL_REFRESH;

  switch (state) {
    case BrowserState::WIFI_SELECTION:
    case BrowserState::SEARCH_INPUT:
      // The picker and the keyboard own the screen.
      view.hidden = true;
      break;
    case BrowserState::CHECK_WIFI:
    case BrowserState::LOADING:
      view.lines = {statusMessage.c_str(), nullptr, nullptr, nullptr};
      break;
    case BrowserState::RECONNECTING:
      view.lines = {reconnectAttempt.empty() ? tr(STR_CONNECTING) : reconnectAttempt.c_str(),
                    reconnectDetail.empty() ? nullptr : reconnectDetail.c_str(), nullptr, nullptr};
      view.backHint = tr(STR_CANCEL);
      break;
    case BrowserState::ERROR:
      view.lines = {tr(STR_ERROR_MSG), errorMessage.empty() ? nullptr : errorMessage.c_str(), nullptr, nullptr};
      view.confirmHint = tr(STR_RETRY);
      break;
    case BrowserState::DOWNLOADING:
      view.lines = {tr(STR_DOWNLOADING), downloadTitle.c_str(), nullptr, nullptr};
      view.showProgress = downloadTotal > 0;
      view.progressValue = static_cast<int>(downloadProgress);
      view.progressMax = downloadTotal > 0 ? static_cast<int>(downloadTotal) : 1;
      view.backHint = tr(STR_CANCEL);
      break;
    case BrowserState::DETAIL: {
      view.sections[0].heading = detailEntry.title.c_str();
      size_t lineIdx = 0;
      if (!detailEntry.author.empty()) view.sections[0].lines[lineIdx++] = detailEntry.author.c_str();
      if (detailOnCard) view.sections[0].lines[lineIdx++] = tr(STR_OPDS_ON_CARD);
      if (!detailEntry.summary.empty()) {
        view.sections[1].paragraph = detailEntry.summary.c_str();
        view.sections[1].paragraphMaxLines = 8;
      }
      view.choices = {tr(STR_DOWNLOAD), tr(STR_OPDS_DOWNLOAD_AND_READ)};
      view.confirmHint = tr(STR_SELECT);
      view.thirdHint = tr(STR_DIR_UP);
      view.fourthHint = tr(STR_DIR_DOWN);
      break;
    }
    case BrowserState::BROWSING:
      if (rows.empty()) {
        view.lines = {tr(STR_NO_ENTRIES), nullptr, nullptr, nullptr};
        break;
      }
      view.listItems = rows.data();
      view.listCount = static_cast<int>(rows.size());
      view.listHasSubtitle = true;
      view.confirmHint = tr(STR_OPEN);
      // Search shares the page-up button, on the first row only, where a feed opens.
      view.thirdHint = !searchTemplate.empty() && listSelection() == 0 ? tr(STR_SEARCH) : tr(STR_DIR_UP);
      view.fourthHint = tr(STR_DIR_DOWN);
      break;
  }
  return view;
}

void OpdsBookBrowserActivity::drawHeaderExtras(const Rect& headerRect) {
  if (searchTemplate.empty() || state == BrowserState::DETAIL) return;
  renderer.drawIcon(Search24Icon.bits, searchIconX(renderer), headerRect.y + (headerRect.height - SEARCH_ICON_SIZE) / 2,
                    Search24Icon.w);
}

void OpdsBookBrowserActivity::afterRender() { pendingFullRefresh = false; }

// --- Input handling ---

bool OpdsBookBrowserActivity::handleCustomInput() {
  if (state == BrowserState::WIFI_SELECTION || state == BrowserState::SEARCH_INPUT) return true;

  // A press that opened this screen must not also act on it.
  if (consumeConfirm && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    consumeConfirm = false;
    return true;
  }

  // Allow Back button to cancel during in-flight operations
  if (state == BrowserState::RECONNECTING || state == BrowserState::DOWNLOADING || state == BrowserState::LOADING) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested = true;
      return true;
    }
    return true;
  }

  // Back acts on release, so a hold skips refetching every level. Only a press seen here
  // arms it, not the one that closed the search keyboard.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) return backArmed = true;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) && std::exchange(backArmed, false)) {
    mappedInput.getHeldTime() >= GO_HOME_MS ? onGoHome() : onBackButton();
    return true;
  }

  if (state == BrowserState::BROWSING && mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    if (!searchTemplate.empty() && listSelection() == 0) {
      launchSearch();
      return true;
    }
  }
  return false;
}

void OpdsBookBrowserActivity::onListActivated(const int index) {
  if (state != BrowserState::BROWSING || index < 0 || index >= static_cast<int>(entries.size())) return;
  setListSelection(index);
  // Copied before the call: navigating and downloading both release the list
  // this entry lives in.
  const OpdsEntry entry = entries[index];
  if (entry.type == OpdsEntryType::BOOK) {
    detailEntry = entry;
    detailOnCard = isBookOnCard(entry);
    setChoiceIndex(missingOnly ? 1 : 0);
    state = BrowserState::DETAIL;
    requestUpdate();
    return;
  }
  navigateToEntry(entry);
}

void OpdsBookBrowserActivity::onChoiceActivated(const int index) {
  if (state != BrowserState::DETAIL) return;
  downloadBook(detailEntry, /*openAfter=*/index == 1);
}

void OpdsBookBrowserActivity::onBackButton() {
  if (state == BrowserState::RECONNECTING || state == BrowserState::DOWNLOADING || state == BrowserState::LOADING) {
    cancelRequested = true;
    return;
  }
  if (state == BrowserState::DETAIL) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }
  if (state == BrowserState::CHECK_WIFI) {
    finish();
    return;
  }
  navigateBack();
}

void OpdsBookBrowserActivity::onConfirmButton() {
  if (state != BrowserState::ERROR) return;
  // A failure retries the feed, unless the link itself is what is missing.
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}
