#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <OpdsStream.h>
#include <WiFi.h>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "components/icons/search24.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/TlsScratchHeap.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace {
constexpr int SEARCH_ICON_SIZE = 24;
constexpr int SEARCH_ICON_MARGIN = 14;
constexpr int DOWNLOAD_PROGRESS_STEP_PERCENT = 5;
constexpr unsigned long DOWNLOAD_PROGRESS_MIN_UPDATE_MS = 5000;

// The search glyph sits inside the header band, at its right edge.
int searchIconX(const GfxRenderer& renderer) {
  return renderer.getScreenWidth() - SEARCH_ICON_SIZE - SEARCH_ICON_MARGIN;
}

}  // namespace

void OpdsBookBrowserActivity::onEnter() {
  UiStatusActivity::onEnter();

  // Browsing an OPDS catalogue is WiFi plus TLS plus a parse, on whatever heap
  // is left after a book's fonts are resident. Release the rebuildable SD font
  // caches first, the way the web server and Calibre paths do: a handshake that
  // runs out of contiguous heap fails as a bad peer key, which reads like a
  // server fault and is not one.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("OPDS", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("OPDS", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = "";
  setListSelection(0);
  consumeConfirm = false;
  consumeBack = false;
  cancelRequested = false;
  reconnectDetail.clear();
  reconnectAttempt.clear();
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
  cancelRequested = true;
  Activity::onExit();
  entries.clear();
  navigationHistory.clear();

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void OpdsBookBrowserActivity::fetchFeed(const std::string& path) {
  if (server.url.empty()) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_NO_SERVER_URL);
    requestUpdate();
    return;
  }

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  cancelRequested = false;
  reconnectDetail.clear();
  reconnectAttempt.clear();
  setListSelection(0);
  releaseEntries();
  vTaskDelay(1);

  opds::FeedResult result;
  std::unique_ptr<GfxRenderer::FrameBufferLoan> loan;
  std::unique_ptr<tls_scratch::Session> tlsScratch;

  auto beforeAttempt = [this, &loan, &tlsScratch]() {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdateAndWait();
    loan = makeUniqueNoThrow<GfxRenderer::FrameBufferLoan>(renderer);
    tlsScratch = makeUniqueNoThrow<tls_scratch::Session>();
  };

  auto afterAttempt = [this, &loan, &tlsScratch]() {
    tlsScratch.reset();
    loan.reset();
    pendingFullRefresh = true;
  };

  auto onReconnect = [this](const opds::ReconnectInfo& info) {
    state = BrowserState::RECONNECTING;
    char attemptBuf[64];
    snprintf(attemptBuf, sizeof(attemptBuf), "%s (%d/%d)...", tr(STR_CONNECTING), info.attempt, info.maxAttempts);
    reconnectDetail = info.reason ? info.reason : tr(STR_CONNECTING);
    reconnectAttempt = attemptBuf;
    requestUpdate();
  };

  auto pollCancel = [this]() -> bool {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested = true;
      return true;
    }
    return cancelRequested;
  };

  const auto status =
      client.fetchFeed(server, path, result, onReconnect, &cancelRequested, opds_retry::DEFAULT_TIMEOUT_MS,
                       opds_retry::MAX_ATTEMPTS, beforeAttempt, afterAttempt, pollCancel);

  if (status == opds::ClientStatus::ABORTED) {
    if (!navigationHistory.empty()) {
      navigateBack();
    } else if (!entries.empty()) {
      state = BrowserState::BROWSING;
      requestUpdate();
    } else {
      state = BrowserState::ERROR;
      errorMessage = tr(STR_CANCEL);
      requestUpdate();
    }
    return;
  }

  if (status == opds::ClientStatus::BAD_CREDENTIALS) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_OPDS_BAD_CREDENTIALS);
    requestUpdate();
    return;
  }

  if (status == opds::ClientStatus::HEAP_LOW) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_UPDATE_LOW_MEMORY);
    requestUpdate();
    return;
  }

  if (status == opds::ClientStatus::PARSE_FAILED) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  if (status != opds::ClientStatus::OK && status != opds::ClientStatus::EMPTY) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = std::move(result.searchTemplate);
  const auto nextUrl = std::move(result.nextPageUrl);
  const auto prevUrl = std::move(result.prevPageUrl);
  const bool feedTruncated = result.truncated;
  entries = std::move(result.entries);

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(), OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, tr(STR_NEXT_PAGE), "", nextUrl, ""});
  }
  if (feedTruncated) {
    LOG_INF("OPDS", "Feed truncated to fit memory");
  }

  {
    RenderLock lock(*this);
    refreshRows();
    setListSelectionLocked(0);
    state = entries.empty() ? BrowserState::ERROR : BrowserState::BROWSING;
    if (entries.empty()) errorMessage = tr(STR_NO_ENTRIES);
  }
  requestUpdate();
}

void OpdsBookBrowserActivity::releaseEntries() {
  // Under the render lock: the rows the render task is walking point into these
  // two vectors, so freeing them from this task mid-paint reads freed memory.
  RenderLock lock(*this);
  std::vector<OpdsEntry>().swap(entries);
  rows.clear();
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  setListSelection(0);
  fetchFeed(currentPath);
}

void OpdsBookBrowserActivity::navigateBack() {
  if (navigationHistory.empty()) {
    onGoHome();
  } else {
    currentPath = navigationHistory.back();
    navigationHistory.pop_back();
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    releaseEntries();
    setListSelection(0);
    requestUpdate();
    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadTitle = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), renderer.getScreenWidth() - 40);
  downloadProgress = downloadTotal = 0;
  cancelRequested = false;
  reconnectDetail.clear();
  reconnectAttempt.clear();

  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;

  auto progressCb = [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    downloadProgress = downloaded;
    downloadTotal = total;
    const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
    const unsigned long now = millis();
    if (percent >= 100 || lastRenderedPercent < 0 || percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
        now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
      lastRenderedPercent = percent;
      lastProgressUpdateMs = now;
      // Deliberately no repaint: the framebuffer belongs to wolfSSL until the
      // loan ends. The counters still move, and the screen catches up after.
    }
  };

  std::unique_ptr<GfxRenderer::FrameBufferLoan> loan;
  std::unique_ptr<tls_scratch::Session> tlsScratch;

  auto beforeAttempt = [this, &loan, &tlsScratch]() {
    state = BrowserState::DOWNLOADING;
    requestUpdateAndWait();
    loan = makeUniqueNoThrow<GfxRenderer::FrameBufferLoan>(renderer);
    tlsScratch = makeUniqueNoThrow<tls_scratch::Session>();
  };

  auto afterAttempt = [this, &loan, &tlsScratch]() {
    tlsScratch.reset();
    loan.reset();
    pendingFullRefresh = true;
  };

  auto onReconnect = [this](const opds::ReconnectInfo& info) {
    state = BrowserState::RECONNECTING;
    char attemptBuf[64];
    snprintf(attemptBuf, sizeof(attemptBuf), "%s (%d/%d)...", tr(STR_CONNECTING), info.attempt, info.maxAttempts);
    reconnectDetail = info.reason ? info.reason : tr(STR_CONNECTING);
    reconnectAttempt = attemptBuf;
    requestUpdate();
  };

  auto pollCancel = [this]() -> bool {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested = true;
      return true;
    }
    return cancelRequested;
  };

  std::string finalPath;
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  const auto result = client.downloadBook(server, feedUrl, book, finalPath, progressCb, &cancelRequested, onReconnect,
                                          opds_retry::DEFAULT_TIMEOUT_MS, opds_retry::MAX_ATTEMPTS, beforeAttempt,
                                          afterAttempt, pollCancel);

  if (result == HttpDownloader::OK) {
    state = BrowserState::BROWSING;
  } else if (result == HttpDownloader::ABORTED) {
    state = BrowserState::BROWSING;
  } else {
    LOG_ERR("OPDS", "Download failed (%d): free %d bytes, largest block %d bytes", static_cast<int>(result),
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    state = BrowserState::ERROR;
    errorMessage = tr(STR_DOWNLOAD_FAILED);
  }
  pendingFullRefresh = true;
  requestUpdate();
}

void OpdsBookBrowserActivity::launchSearch() {
  consumeConfirm = true;
  state = BrowserState::SEARCH_INPUT;
  requestUpdate();

  auto keyboard = std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, tr(STR_SEARCH));
  startActivityForResult(std::move(keyboard), [this](const ActivityResult& result) {
    state = BrowserState::BROWSING;
    if (!result.isCancelled) {
      performSearch(std::get<KeyboardResult>(result.data).text);
    } else {
      requestUpdate();
    }
  });
}

void OpdsBookBrowserActivity::performSearch(const std::string& query) {
  if (query.empty() || searchTemplate.empty()) {
    state = BrowserState::BROWSING;
    requestUpdate();
    return;
  }

  auto urlEncode = [](const std::string& s) {
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
      if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        out += static_cast<char>(c);
      else {
        char buf[4];
        snprintf(buf, sizeof(buf), "%%%02X", c);
        out += buf;
      }
    }
    return out;
  };

  std::string url = searchTemplate;
  const std::string placeholder = "{searchTerms}";
  const size_t pos = url.find(placeholder);
  if (pos != std::string::npos) url.replace(pos, placeholder.length(), urlEncode(query));

  navigationHistory.push_back(currentPath);  // <-- add this
  currentPath = url;                         // <-- add this

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  releaseEntries();
  setListSelection(0);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    requestUpdate();
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}

void OpdsBookBrowserActivity::launchWifiSelection() {
  state = BrowserState::WIFI_SELECTION;
  requestUpdate();

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OpdsBookBrowserActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
    fetchFeed(currentPath);
  } else {
    // Leave WiFi up; onExit's silent reboot handles teardown without fragmenting.
    state = BrowserState::ERROR;
    errorMessage = tr(STR_WIFI_CONN_FAILED);
    requestUpdate();
  }
}

// --- Rows and screen ---

void OpdsBookBrowserActivity::refreshRows() {
  // The rows borrow the feed's own strings rather than copying them: a large
  // catalogue already costs what its titles cost, and a second copy of every
  // title and author is heap this device does not have with WiFi and TLS up.
  rows.resize(entries.size());
  for (size_t i = 0; i < entries.size(); ++i) {
    const OpdsEntry& entry = entries[i];
    rows[i] = freeink::ui::ListItem{};
    rows[i].label = entry.title.c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
    // A feed mixes places to go with books to take. The arrow marks the first,
    // and a book carries its author under the title.
    if (entry.type == OpdsEntryType::NAVIGATION) {
      rows[i].value = ">";
    } else if (!entry.author.empty()) {
      rows[i].subtitle = entry.author.c_str();
    }
  }
}

UiStatusActivity::StatusView OpdsBookBrowserActivity::statusView() const {
  StatusView view;
  view.title = server.name.empty() ? tr(STR_OPDS_BROWSER) : server.name.c_str();
  // The framebuffer comes back white from a TLS loan, and the download screen
  // repaints in FAST on every step, so the frame after either owes a full pass.
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
    case BrowserState::BROWSING:
      if (rows.empty()) {
        view.lines = {tr(STR_NO_ENTRIES), nullptr, nullptr, nullptr};
        break;
      }
      view.listItems = rows.data();
      view.listCount = static_cast<int>(rows.size());
      view.listHasSubtitle = true;
      // The selection is clamped when the list is built, which is after this
      // runs: on the first paint of a shorter feed it can still name a row that
      // is no longer there.
      {
        const size_t selected = static_cast<size_t>(listSelection());
        const bool isBook = selected < entries.size() && entries[selected].type == OpdsEntryType::BOOK;
        view.confirmHint = isBook ? tr(STR_DOWNLOAD) : tr(STR_OPEN);
      }
      // The search sits on the same button the list pages up with, and only on
      // the first row, which is where the reader lands when a feed opens.
      view.thirdHint = !searchTemplate.empty() && listSelection() == 0 ? tr(STR_SEARCH) : tr(STR_DIR_UP);
      view.fourthHint = tr(STR_DIR_DOWN);
      break;
  }
  return view;
}

void OpdsBookBrowserActivity::drawHeaderExtras(const Rect& headerRect) {
  if (searchTemplate.empty()) return;
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
  if (consumeBack && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    consumeBack = false;
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
  entry.type == OpdsEntryType::BOOK ? downloadBook(entry) : navigateToEntry(entry);
}

void OpdsBookBrowserActivity::onBackButton() {
  if (state == BrowserState::RECONNECTING || state == BrowserState::DOWNLOADING || state == BrowserState::LOADING) {
    cancelRequested = true;
    return;
  }
  if (state == BrowserState::CHECK_WIFI) {
    onGoHome();
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
