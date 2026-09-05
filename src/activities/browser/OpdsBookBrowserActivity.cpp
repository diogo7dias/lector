#include "OpdsBookBrowserActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
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
  errorMessage.clear();
  statusMessage = tr(STR_CHECKING_WIFI);
  requestUpdate();

  checkAndConnectWifi();
}

void OpdsBookBrowserActivity::onExit() {
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
  setListSelectionLocked(0);
  releaseEntries();
  vTaskDelay(1);

  std::string url = UrlUtils::buildUrl(server.url, path);
  LOG_DBG("OPDS", "Fetching: %s", url.c_str());
  OpdsParser parser;
  bool fetched = false;
  auto fetchError = HttpDownloader::OK;
  int fetchStatus = 0;
  {
    OpdsParserStream stream{parser};
    // Any paint already asked for has to land BEFORE the bytes are lent: the render
    // task draws on its own, and the framebuffer is not there to draw into.
    requestUpdateAndWait();
    // The framebuffer's 48 KB go to wolfSSL for the length of the fetch, the same
    // loan the font download uses. A feed is read over TLS from a server that may
    // well send 16 KB records, and the receive buffer for one of those does not fit
    // in the heap WiFi leaves behind. Nothing draws while the bytes are lent, which
    // costs nothing here: the panel already shows "Loading".
    GfxRenderer::FrameBufferLoan loan(renderer);
    const tls_scratch::Session tlsScratch;
    fetched = HttpDownloader::fetchUrl(url, stream, server.username, server.password, &fetchError, &fetchStatus);
  }
  // The loan hands the framebuffer back white, so whatever comes next repaints in full.
  pendingFullRefresh = true;
  if (!fetched) {
    LOG_ERR("OPDS", "Fetch failed (error %d, status %d, credentials %s): free %d bytes, largest block %d bytes",
            fetchError, fetchStatus, server.username.empty() ? "none" : "sent", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
    state = BrowserState::ERROR;
    // 401 is the server answering, not the reader failing to reach it, and it has
    // exactly one cure. Saying so beats a generic failure the reader cannot act on.
    errorMessage = fetchStatus == 401 ? tr(STR_OPDS_BAD_CREDENTIALS) : tr(STR_FETCH_FEED_FAILED);
    requestUpdate();
    return;
  }

  if (!parser) {
    state = BrowserState::ERROR;
    errorMessage = tr(STR_PARSE_FEED_FAILED);
    requestUpdate();
    return;
  }

  searchTemplate = parser.getSearchTemplate();
  const auto& nextUrl = parser.getNextPageUrl();
  const auto& prevUrl = parser.getPrevPageUrl();
  const bool feedTruncated = parser.truncated();
  // The vector the rows will point into is built before the lock below; nothing
  // draws from it until that lock publishes the rows with it.
  entries = std::move(parser).getEntries();

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
    // One lock over the whole swap: the rows, the selection they are indexed by
    // and the state that says to draw them all have to change together, or the
    // render task can paint a row list against the wrong feed.
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
  entries.clear();
  entries.shrink_to_fit();
  rows.clear();
  rows.shrink_to_fit();
}

void OpdsBookBrowserActivity::navigateToEntry(const OpdsEntry& entry) {
  navigationHistory.push_back(currentPath);
  // Resolve to a full URL so sub-sub-navigation retains parent path context
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  currentPath = UrlUtils::buildUrl(feedUrl, entry.href);

  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  setListSelectionLocked(0);
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
    setListSelectionLocked(0);
    fetchFeed(currentPath);
  }
}

void OpdsBookBrowserActivity::downloadBook(const OpdsEntry& book) {
  state = BrowserState::DOWNLOADING;
  statusMessage = book.title;
  downloadTitle = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), renderer.getScreenWidth() - 40);
  downloadProgress = downloadTotal = 0;
  releaseEntries();
  vTaskDelay(1);

  // Build full download URL relative to the current feed, not the root server URL
  const std::string feedUrl = UrlUtils::buildUrl(server.url, currentPath);
  std::string downloadUrl = UrlUtils::buildUrl(feedUrl, book.href);
  // opdsDownloadFolder is already a null-terminated char[64]; use it directly —
  // no std::string copy. exists()/mkdir() take const char*.
  const char* folder = SETTINGS.opdsDownloadFolder;  // "" => SD root
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    // exists()-guard first: mkdir's return-on-existing is unconfirmed, and every
    // existing caller checks exists() before mkdir. On real failure, fall back
    // to SD root so the download is never lost.
    LOG_ERR("OPDS", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }

  // downloadToFile() needs a std::string, and titles are unbounded (a fixed
  // char[] would truncate). Cold path (a multi-second download follows), so one
  // reserve'd, in-place-appended owning string is the right call.
  std::string filename;
  filename.reserve(96);
  if (haveFolder) filename += folder;
  filename += '/';
  filename += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  LOG_DBG("OPDS", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  int lastRenderedPercent = -1;
  unsigned long lastProgressUpdateMs = 0;
  HttpDownloader::DownloadError result;
  {
    requestUpdateAndWait();
    // Same loan as the feed fetch and the font download: a book arrives over TLS
    // and the record buffer does not fit in what WiFi leaves. The progress bar
    // holds at 0 for the transfer, which is the price of the file arriving at all.
    GfxRenderer::FrameBufferLoan loan(renderer);
    const tls_scratch::Session tlsScratch;
    result = HttpDownloader::downloadToFile(
        downloadUrl, filename,
        [this, &lastRenderedPercent, &lastProgressUpdateMs](const size_t downloaded, const size_t total) {
          downloadProgress = downloaded;
          downloadTotal = total;
          const int percent = total > 0 ? static_cast<int>(static_cast<uint64_t>(downloaded) * 100 / total) : 0;
          const unsigned long now = millis();
          if (percent >= 100 || lastRenderedPercent < 0 ||
              percent >= lastRenderedPercent + DOWNLOAD_PROGRESS_STEP_PERCENT ||
              now - lastProgressUpdateMs >= DOWNLOAD_PROGRESS_MIN_UPDATE_MS) {
            lastRenderedPercent = percent;
            lastProgressUpdateMs = now;
            // Deliberately no repaint: the framebuffer belongs to wolfSSL until the
            // loan ends. The counters still move, and the screen catches up after.
          }
        },
        nullptr, server.username, server.password);
  }

  if (result == HttpDownloader::OK) {
    clearBookCache(filename);
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
  setListSelectionLocked(0);
  fetchFeed(url);
}

void OpdsBookBrowserActivity::checkAndConnectWifi() {
  if (WiFi.status() == WL_CONNECTED && WiFi.localIP() != IPAddress(0, 0, 0, 0)) {
    state = BrowserState::LOADING;
    statusMessage = tr(STR_LOADING);
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
    case BrowserState::ERROR:
      view.lines = {tr(STR_ERROR_MSG), errorMessage.empty() ? nullptr : errorMessage.c_str(), nullptr, nullptr};
      view.confirmHint = tr(STR_RETRY);
      break;
    case BrowserState::DOWNLOADING:
      view.lines = {tr(STR_DOWNLOADING), downloadTitle.c_str(), nullptr, nullptr};
      view.showProgress = downloadTotal > 0;
      view.progressValue = static_cast<int>(downloadProgress);
      view.progressMax = downloadTotal > 0 ? static_cast<int>(downloadTotal) : 1;
      view.backHint = "";
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

  // Nothing to answer while a feed or a book is in flight.
  if (state == BrowserState::DOWNLOADING || state == BrowserState::LOADING) return true;

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
  state = BrowserState::LOADING;
  statusMessage = tr(STR_LOADING);
  setListSelectionLocked(index);
  // Copied before the call: navigating and downloading both release the list
  // this entry lives in.
  const OpdsEntry entry = entries[index];
  entry.type == OpdsEntryType::BOOK ? downloadBook(entry) : navigateToEntry(entry);
}

void OpdsBookBrowserActivity::onBackButton() {
  if (state == BrowserState::DOWNLOADING || state == BrowserState::LOADING) return;
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
    setListSelectionLocked(0);
    fetchFeed(currentPath);
    return;
  }
  launchWifiSelection();
}
