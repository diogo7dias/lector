#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cassert>
#include <cmath>

#include "Epub/Section.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"

namespace {
std::string calculateDocumentHashForMethod(const std::string& path, const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? KOReaderDocumentId::calculateFromFilename(path)
                                                 : KOReaderDocumentId::calculate(path);
}

DocumentMatchMethod alternateMatchMethod(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? DocumentMatchMethod::BINARY : DocumentMatchMethod::FILENAME;
}

const char* matchMethodName(const DocumentMatchMethod method) {
  return method == DocumentMatchMethod::FILENAME ? "filename" : "binary";
}

void syncTimeWithNTP() {
  // Stop SNTP if already running (can't reconfigure while running)
  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }

  // Configure SNTP
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  // Wait for time to sync (with timeout)
  int retry = 0;
  const int maxRetries = 50;  // 5 seconds max
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < maxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    retry++;
  }

  if (retry < maxRetries) {
    LOG_DBG("KOSync", "NTP time synced");
  } else {
    LOG_DBG("KOSync", "NTP sync timeout, using fallback");
  }
}
}  // namespace

void KOReaderSyncActivity::ensureEpubLoaded() {
  if (!epub) {
    LOG_DBG("KOSync", "Loading epub for progress mapping (heap: %u)", (unsigned)ESP.getFreeHeap());
    epub = std::make_shared<Epub>(epubPath, "/.crosspoint");
    epub->setupCacheDir();
    // Load metadata only (no CSS needed for progress mapping, don't rebuild if cache is missing).
    if (!epub->load(false, true)) {
      LOG_ERR("KOSync", "Failed to load epub for progress mapping");
      epub.reset();
      return;
    }
    LOG_DBG("KOSync", "Epub loaded (heap: %u)", (unsigned)ESP.getFreeHeap());
  }
}

void KOReaderSyncActivity::saveProgressAndReturn(int spineIndex, int page) {
  // epub is guaranteed non-null here: ensureEpubLoaded() was called in performSync() before
  // SHOWING_RESULT state is entered, and this method is only called from that state.
  assert(epub);
  std::optional<uint32_t> offset;
  if (remotePosition.hasVisibleTextOffset && remotePosition.spineIndex == spineIndex) {
    offset = remotePosition.visibleTextOffset;
  }
  if (!EpubReaderUtils::saveProgress(*epub, spineIndex, page, 0, offset)) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_SAVE_PROGRESS_FAILED);
    }
    requestUpdate(true);
    return;
  }
  returnToReader();
}

void KOReaderSyncActivity::returnToReader() { activityManager.goToReader(epubPath); }

bool KOReaderSyncActivity::smartSyncEnabled() const {
  return KOREADER_STORE.getSyncBehavior() == KOReaderSyncBehavior::SMART;
}

void KOReaderSyncActivity::markAutoReturn() { autoReturnAt = millis() + AUTO_RETURN_DELAY_MS; }

void KOReaderSyncActivity::completeAlreadySynced() {
  {
    RenderLock lock(*this);
    state = SYNC_COMPLETE;
  }
  markAutoReturn();
  requestUpdate(true);
}

void KOReaderSyncActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_DBG("KOSync", "WiFi connection failed, exiting");
    returnToReader();
    return;
  }

  LOG_DBG("KOSync", "WiFi connected, starting sync");

  {
    RenderLock lock(*this);
    state = SYNCING;
    statusMessage = tr(STR_SYNCING_TIME);
  }
  requestUpdate(true);

  // Keep the station fully awake for the short sync transaction: modem sleep can
  // introduce multi-second network stalls that surface as HTTP timeouts. WiFi is torn
  // down when this activity exits. Upstream #3233.
  WiFi.setSleep(false);

  // Sync time with NTP before making API requests
  syncTimeWithNTP();

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_CALC_HASH);
  }
  requestUpdate(true);

  performSync();
}

void KOReaderSyncActivity::performSync() {
  const DocumentMatchMethod primaryMethod = KOREADER_STORE.getMatchMethod();
  documentHash = calculateDocumentHashForMethod(epubPath, primaryMethod);
  if (documentHash.empty()) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = tr(STR_HASH_FAILED);
    }
    requestUpdate(true);
    return;
  }
  const std::string primaryHash = documentHash;

  LOG_DBG("KOSync", "Document hash (%s): %s", matchMethodName(primaryMethod), documentHash.c_str());

  {
    RenderLock lock(*this);
    statusMessage = tr(STR_FETCH_PROGRESS);
  }
  requestUpdateAndWait();

  // Fetch remote progress. In smart mode, also probe the alternate document-id
  // method and use the furthest remote state we can find. This avoids a stale
  // local upload when another KOReader device synced the same book with a
  // different document matching method.
  auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);
  LOG_DBG("KOSync", "Primary remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
          matchMethodName(primaryMethod), result, KOReaderSyncClient::lastHttpCode, documentHash.c_str(),
          localProgress.percentage, remoteProgress.percentage, remoteProgress.progress.c_str());

  if (smartSyncEnabled()) {
    const DocumentMatchMethod altMethod = alternateMatchMethod(primaryMethod);
    const std::string altHash = calculateDocumentHashForMethod(epubPath, altMethod);
    if (!altHash.empty() && altHash != documentHash) {
      KOReaderProgress altProgress;
      const auto altResult = KOReaderSyncClient::getProgress(altHash, altProgress);
      LOG_DBG("KOSync", "Alternate remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
              matchMethodName(altMethod), altResult, KOReaderSyncClient::lastHttpCode, altHash.c_str(),
              localProgress.percentage, altProgress.percentage, altProgress.progress.c_str());

      if (altResult == KOReaderSyncClient::OK &&
          (result == KOReaderSyncClient::NOT_FOUND || altProgress.percentage > remoteProgress.percentage)) {
        documentHash = altHash;
        remoteProgress = std::move(altProgress);
        result = KOReaderSyncClient::OK;
      }
    }
  }

  if (result == KOReaderSyncClient::NOT_FOUND) {
    if (smartSyncEnabled()) {
      LOG_DBG("KOSync", "Smart sync: no remote progress found for known document hashes; uploading local %.6f",
              localProgress.percentage);
      performUpload();
      return;
    }

    // No remote progress - offer to upload
    {
      RenderLock lock(*this);
      state = NO_REMOTE_PROGRESS;
      hasRemoteProgress = false;
    }
    requestUpdate(true);
    return;
  }

  if (result != KOReaderSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
    }
    requestUpdate(true);
    return;
  }

  // Epub was released before sync to free RAM for the TLS handshake — reload it now.
  hasRemoteProgress = true;
  ensureEpubLoaded();
  if (!epub) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = "";
    }
    requestUpdate(true);
    return;
  }

  // The standard KOReader progress XPath is the authoritative content anchor.
  // The CrossPoint server's existing rich page hints remain a legacy fallback.
  SavedProgressPosition koPos = {remoteProgress.progress, remoteProgress.percentage};
  remotePosition = ProgressMapper::toCrossPoint(epub, koPos, renderer, currentSpineIndex, totalPagesInSpine);
  if (!remotePosition.hasVisibleTextOffset && remoteProgress.position.has_value()) {
    // toCrossPoint above already tried koPos.xpath; if the rich position carries the same XPath,
    // tell fromRichPosition to skip re-resolving it and use its page hints directly.
    const bool sameXPath = remoteProgress.position->xpath == remoteProgress.progress;
    if (const auto richMapped = ProgressMapper::fromRichPosition(epub, *remoteProgress.position, renderer, sameXPath)) {
      remotePosition = *richMapped;
    }
  }

  if (smartSyncEnabled()) {
    static constexpr float SAME_PROGRESS_EPSILON = 0.001f;  // 0.1 percentage points
    const float delta = localProgress.percentage - remoteProgress.percentage;
    LOG_DBG("KOSync", "Smart decision: doc=%s local=%.6f remote=%.6f delta=%.6f remoteXpath=%s mapped=%d/%d",
            documentHash.c_str(), localProgress.percentage, remoteProgress.percentage, delta,
            remoteProgress.progress.c_str(), remotePosition.spineIndex, remotePosition.pageNumber);
    if (std::fabs(delta) <= SAME_PROGRESS_EPSILON) {
      completeAlreadySynced();
      return;
    }

    if (delta > 0) {
      // Alternate hashes are only probes for newer remote state. Keep uploads
      // on the user's configured matching method so its primary record heals.
      documentHash = primaryHash;
      performUpload();
      return;
    }

    saveProgressAndReturn(remotePosition.spineIndex, remotePosition.pageNumber);
    return;
  }

  // localProgress was pre-computed in EpubReaderActivity before the Epub was released.
  prepareComparison();
  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;
    // Open on the answer that keeps the furthest progress.
    setChoiceIndex(localProgress.percentage > remoteProgress.percentage ? 1 : 0);
  }
  requestUpdate(true);
}

void KOReaderSyncActivity::performUpload() {
  {
    RenderLock lock(*this);
    state = UPLOADING;
    statusMessage = tr(STR_UPLOAD_PROGRESS);
  }
  requestUpdateAndWait();

  // localProgress was pre-computed in EpubReaderActivity before the Epub was released.
  KOReaderProgress progress;
  progress.document = documentHash;
  progress.progress = localProgress.xpath;
  progress.percentage = localProgress.percentage;

  // Rich CrossPoint position for the default CrossPoint sync server (lossless
  // CrossPoint<->CrossPoint sync). The HTTP client also enforces this boundary
  // before serializing the extension.
  if (KOREADER_STORE.usesCrossPointSyncServer()) {
    KOReaderRichPosition pos;
    const float pct = localProgress.percentage < 0.0f   ? 0.0f
                      : localProgress.percentage > 1.0f ? 1.0f
                                                        : localProgress.percentage;
    pos.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    pos.spineIndex = static_cast<uint16_t>(currentSpineIndex);
    pos.pageNumber = static_cast<uint16_t>(currentPage);
    pos.totalPages = static_cast<uint16_t>(totalPagesInSpine > 0 ? totalPagesInSpine : 1);
    pos.paragraphIndex = currentParagraphIndex;
    pos.xpath = localProgress.xpath;
    progress.position = std::move(pos);
  }

  // Optionally include document metadata (KOReader PR #15306)
  if (KOREADER_STORE.getSendMetadata()) {
    // The Epub is released before the sync network calls and is only reloaded on the
    // remote-progress path (performSync). When uploading from NO_REMOTE_PROGRESS the
    // Epub is still null, so reload it here and guard the title/author reads to avoid
    // dereferencing a null Epub. Filename is derived from the path and is always safe.
    ensureEpubLoaded();
    KOReaderMetadata meta;
    const auto lastSlash = epubPath.rfind('/');
    meta.filename = (lastSlash != std::string::npos) ? epubPath.substr(lastSlash + 1) : epubPath;
    if (epub) {
      meta.title = epub->getTitle();
      meta.authors = epub->getAuthor();
    } else {
      LOG_ERR("KOSync", "Epub unavailable for metadata; sending filename only");
    }
    progress.metadata = std::move(meta);
  }

  // Release the Epub before the network call so the TLS handshake has enough free heap
  // (consistent with the release-before-sync pattern in performSync); nothing below needs it.
  epub.reset();

  const auto result = KOReaderSyncClient::updateProgress(progress);

  // Drop the radio while user reads the result; full teardown happens at silent reboot.
  esp_wifi_stop();

  if (result != KOReaderSyncClient::OK) {
    {
      RenderLock lock(*this);
      state = SYNC_FAILED;
      statusMessage = KOReaderSyncClient::errorString(result);
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = UPLOAD_COMPLETE;
  }
  markAutoReturn();
  requestUpdate(true);
}

void KOReaderSyncActivity::onEnter() {
  UiStatusActivity::onEnter();
  ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);

  // Check for credentials first
  if (!KOREADER_STORE.hasCredentials()) {
    state = NO_CREDENTIALS;
    requestUpdate();
    return;
  }

  // Past this point every path uses WiFi.
  wifiActivated = true;

  // Check if already connected (e.g. from settings page auth)
  if (WiFi.status() == WL_CONNECTED) {
    LOG_DBG("KOSync", "Already connected to WiFi");
    onWifiSelectionComplete(true);
    return;
  }

  // Launch WiFi selection subactivity
  LOG_DBG("KOSync", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void KOReaderSyncActivity::onExit() {
  Activity::onExit();

  if (wifiActivated) {
    WiFi.disconnect(false);
    delay(30);
    silentRestartToReader();
  }
}

void KOReaderSyncActivity::prepareComparison() {
  // Remote chapter name requires the Epub, loaded lazily in performSync() before
  // this runs; the local one was pre-computed before the book was released.
  const int remoteTocIndex = epub ? epub->getTocIndexForSpineIndex(remotePosition.spineIndex) : -1;
  const std::string remoteChapter =
      (remoteTocIndex >= 0) ? epub->getTocItem(remoteTocIndex).title
                            : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(remotePosition.spineIndex + 1));
  const std::string localChapter = !localChapterName.empty()
                                       ? localChapterName
                                       : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(currentSpineIndex + 1));

  char buffer[128];
  remoteChapterLine = remoteChapter;
  snprintf(buffer, sizeof(buffer), tr(STR_PAGE_OVERALL_FORMAT), remotePosition.pageNumber + 1,
           remoteProgress.percentage * 100);
  remotePageLine = buffer;
  if (!remoteProgress.device.empty()) {
    snprintf(buffer, sizeof(buffer), tr(STR_DEVICE_FROM_FORMAT), remoteProgress.device.c_str());
    remoteDeviceLine = buffer;
  } else {
    remoteDeviceLine.clear();
  }

  localChapterLine = localChapter;
  snprintf(buffer, sizeof(buffer), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), currentPage + 1, totalPagesInSpine,
           localProgress.percentage * 100);
  localPageLine = buffer;
}

UiStatusActivity::StatusView KOReaderSyncActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_KOREADER_SYNC);
  switch (state) {
    case NO_CREDENTIALS:
      view.lines = {tr(STR_NO_CREDENTIALS_MSG), tr(STR_KOREADER_SETUP_HINT), nullptr, nullptr};
      break;
    case CONNECTING:
    case SYNCING:
    case UPLOADING:
      view.lines = {statusMessage.c_str(), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case SHOWING_RESULT:
      view.comparisonHeadline = tr(STR_PROGRESS_FOUND);
      view.comparison[0].label = tr(STR_REMOTE_LABEL);
      view.comparison[0].lines = {remoteChapterLine.c_str(), remotePageLine.c_str(),
                                  remoteDeviceLine.empty() ? nullptr : remoteDeviceLine.c_str()};
      view.comparison[1].label = tr(STR_LOCAL_LABEL);
      view.comparison[1].lines = {localChapterLine.c_str(), localPageLine.c_str(), nullptr};
      view.choices = {tr(STR_APPLY_REMOTE), tr(STR_UPLOAD_LOCAL)};
      view.confirmHint = tr(STR_SELECT);
      break;
    case NO_REMOTE_PROGRESS:
      view.lines = {tr(STR_NO_REMOTE_MSG), tr(STR_UPLOAD_PROMPT), nullptr, nullptr};
      view.confirmHint = tr(STR_UPLOAD);
      break;
    case UPLOAD_COMPLETE:
    case SYNC_COMPLETE:
      view.lines = {state == UPLOAD_COMPLETE ? tr(STR_UPLOAD_SUCCESS) : tr(STR_ALREADY_SYNCED), nullptr, nullptr,
                    nullptr};
      view.confirmHint = tr(STR_DONE);
      break;
    case SYNC_FAILED:
      view.lines = {tr(STR_SYNC_FAILED_MSG), statusMessage.empty() ? nullptr : statusMessage.c_str(), nullptr, nullptr};
      break;
    case WIFI_SELECTION:
      // The WiFi picker owns the screen.
      view.hidden = true;
      break;
  }
  return view;
}

bool KOReaderSyncActivity::handleCustomInput() {
  // A smart sync that finished on its own returns to the book by itself, after
  // long enough for the outcome to be read.
  if (autoReturnAt != 0 && millis() >= autoReturnAt) {
    returnToReader();
    return true;
  }
  // Nothing to answer while the radio is working, and the picker owns its own
  // input.
  return state == WIFI_SELECTION || state == CONNECTING || state == SYNCING || state == UPLOADING;
}

void KOReaderSyncActivity::onBackButton() { returnToReader(); }

void KOReaderSyncActivity::onConfirmButton() {
  if (state == NO_REMOTE_PROGRESS) {
    // Calculate hash if not done yet
    if (documentHash.empty()) {
      documentHash = calculateDocumentHashForMethod(epubPath, KOREADER_STORE.getMatchMethod());
    }
    performUpload();
    return;
  }
  if (state == NO_CREDENTIALS || state == SYNC_FAILED || state == UPLOAD_COMPLETE || state == SYNC_COMPLETE) {
    returnToReader();
  }
}

void KOReaderSyncActivity::onChoiceActivated(const int index) {
  if (state != SHOWING_RESULT) return;
  if (index == 0) {
    saveProgressAndReturn(remotePosition.spineIndex, remotePosition.pageNumber);
    return;
  }
  performUpload();
}
