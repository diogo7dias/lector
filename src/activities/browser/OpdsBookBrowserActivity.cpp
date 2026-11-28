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

#include <utility>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "activities/util/TrustCertificatePrompt.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/TlsScratchHeap.h"
#include "network/opds/BookOrbitMissing.h"
#include "util/BookCacheUtils.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

void OpdsBookBrowserActivity::onEnter() {
  UiStatusActivity::onEnter();

  // WiFi, TLS and a parse run on what a book's fonts leave: release the rebuildable SD font caches
  // first, as the web server and Calibre do. A handshake short of heap reads like a server fault.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("OPDS", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("OPDS", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  state = BrowserState::CHECK_WIFI;
  entries.clear();
  navigationHistory.clear();
  searchTemplate = "";
  currentPath = startPath;
  openDownloadedBook = false;
  setListSelection(0);
  consumeConfirm = false;
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

  teardownWifiAndRestart(openDownloadedBook ? silentRestartToReader : silentRestartBack);
}

// The error screen's text for a failed fetch; nullptr when the feed arrived, even empty.
// ABORTED is handled before this is asked.
static const char* feedErrorMessage(const opds::ClientStatus status) {
  switch (status) {
    case opds::ClientStatus::OK:
    case opds::ClientStatus::EMPTY:
      return nullptr;
    case opds::ClientStatus::BAD_CREDENTIALS:
      return tr(STR_OPDS_BAD_CREDENTIALS);
    case opds::ClientStatus::REDIRECTED:
      return tr(STR_OPDS_REDIRECTED);
    case opds::ClientStatus::SERVER_ERROR:
      return tr(STR_OPDS_SERVER_ERROR);
    case opds::ClientStatus::FEED_TOO_LARGE:
      return tr(STR_OPDS_FEED_TOO_LARGE);
    case opds::ClientStatus::HEAP_LOW:
      return tr(STR_UPDATE_LOW_MEMORY);
    case opds::ClientStatus::PARSE_FAILED:
      return tr(STR_PARSE_FEED_FAILED);
    case opds::ClientStatus::UNREACHABLE:
      return tr(STR_SERVER_UNREACHABLE);
    default:
      return tr(STR_FETCH_FEED_FAILED);
  }
}

// Shared by the feed fetch and the book download: the retry screen, and Back as cancel.
void OpdsBookBrowserActivity::showReconnect(const opds::ReconnectInfo& info) {
  state = BrowserState::RECONNECTING;
  char attemptBuf[64];
  snprintf(attemptBuf, sizeof(attemptBuf), "%s (%d/%d)...", tr(STR_CONNECTING), info.attempt, info.maxAttempts);
  reconnectDetail = info.reason ? info.reason : tr(STR_CONNECTING);
  reconnectAttempt = attemptBuf;
  requestUpdate();
}

bool OpdsBookBrowserActivity::pollBackForCancel() {
  mappedInput.update();
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    cancelRequested = true;
    return true;
  }
  return cancelRequested;
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

  const auto onReconnect = [this](const opds::ReconnectInfo& info) { showReconnect(info); };
  const auto pollCancel = [this] { return pollBackForCancel(); };

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

  if (status == opds::ClientStatus::CERT_UNTRUSTED &&
      askToTrustRejectedCertificate(*this, renderer, mappedInput, [this, path](const bool trusted) {
        if (trusted) {
          fetchFeed(path);
          return;
        }
        state = BrowserState::ERROR;
        errorMessage = tr(STR_CERT_UNTRUSTED);
        requestUpdate();
      })) {
    return;
  }

  if (const char* error = feedErrorMessage(status)) {
    state = BrowserState::ERROR;
    errorMessage = error;
    requestUpdate();
    return;
  }

  searchTemplate = std::move(result.searchTemplate);
  const auto nextUrl = std::move(result.nextPageUrl);
  const auto prevUrl = std::move(result.prevPageUrl);
  const bool feedTruncated = result.truncated;
  entries = std::move(result.entries);
  if (missingOnly) {
    const size_t missing = bookorbit_missing::keepMissing(entries);
    if (navigationHistory.empty()) bookorbit_missing::save(static_cast<int>(missing), startPath);
  }

  entries.reserve(entries.size() + (prevUrl.empty() ? 0 : 1) + (nextUrl.empty() ? 0 : 1));
  if (!prevUrl.empty()) {
    entries.insert(entries.begin(),
                   OpdsEntry{OpdsEntryType::NAVIGATION, OpdsBookFormat::EPUB, tr(STR_PREV_PAGE), "", prevUrl, ""});
  }
  if (!nextUrl.empty()) {
    entries.push_back(OpdsEntry{OpdsEntryType::NAVIGATION, OpdsBookFormat::EPUB, tr(STR_NEXT_PAGE), "", nextUrl, ""});
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
  // Under the render lock: the rows being painted point into these vectors.
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
    finish();
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

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book, const bool openAfter) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadTitle = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), renderer.getScreenWidth() - 40);
  downloadProgress = downloadTotal = 0;
  cancelRequested = false;
  reconnectDetail.clear();
  reconnectAttempt.clear();

  auto progressCb = [this](const size_t downloaded, const size_t total) {
    mappedInput.update();
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested = true;
    }
    downloadProgress = downloaded;
    downloadTotal = total;
    // No repaint: wolfSSL holds the framebuffer until the loan ends.
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

  const auto onReconnect = [this](const opds::ReconnectInfo& info) { showReconnect(info); };
  const auto pollCancel = [this] { return pollBackForCancel(); };

  std::string finalPath;
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  const auto result = client.downloadBook(server, feedUrl, book, finalPath, progressCb, &cancelRequested, onReconnect,
                                          opds_retry::DEFAULT_TIMEOUT_MS, opds_retry::MAX_ATTEMPTS, beforeAttempt,
                                          afterAttempt, pollCancel);

  if (result == HttpDownloader::CERT_UNTRUSTED &&
      askToTrustRejectedCertificate(*this, renderer, mappedInput, [this, retry = book, openAfter](const bool trusted) {
        if (trusted) {
          downloadBook(retry, openAfter);
          return;
        }
        state = BrowserState::ERROR;
        errorMessage = tr(STR_CERT_UNTRUSTED);
        requestUpdate();
      })) {
    return;
  }

  if (result == HttpDownloader::OK && (missingOnly || openAfter)) {
    // Open it so Auto Sync pulls its position; it is still listed, hence -1.
    if (missingOnly && navigationHistory.empty())
      bookorbit_missing::save(static_cast<int>(bookorbit_missing::countBooks(entries)) - 1, startPath);
    APP_STATE.openEpubPath = finalPath;
    openDownloadedBook = true;
    activityManager.goToReader(finalPath);
    return;
  }
  if (result == HttpDownloader::OK) {
    {
      RenderLock lock(*this);
      refreshRows();
    }
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

  navigationHistory.push_back(currentPath);
  currentPath = url;

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
