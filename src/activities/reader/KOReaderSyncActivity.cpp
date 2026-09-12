#include "KOReaderSyncActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cassert>

#include "Epub/Section.h"
#include "EpubReaderUtils.h"
#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "MappedInputManager.h"
#include "ProgressComparison.h"
#include "ReaderUtils.h"
#include "SilentRestart.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "util/KOReaderSyncMessage.h"

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

}  // namespace

void KOReaderSyncActivity::ensureEpubLoaded() {
  if (!epub) {
    LOG_DBG("KOSync", "Loading epub for progress mapping (heap: %u)", (unsigned)ESP.getFreeHeap());
    epub = makeUniqueNoThrow<Epub>(epubPath, "/.crosspoint");
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

void KOReaderSyncActivity::applyRemoteProgress(int spineIndex, int page) {
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
  // Moving the reader's position is the one destructive thing a sync does, so
  // it is never silent: say it happened, then return to the book the way an
  // upload does.
  {
    RenderLock lock(*this);
    state = SYNC_COMPLETE;
    appliedRemote = true;
  }
  markAutoReturn();
  requestUpdate(true);
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
    statusMessage = tr(STR_CALC_HASH);
  }
  requestUpdate(true);

  // Keep the station fully awake for the short sync transaction: modem sleep can
  // introduce multi-second network stalls that surface as HTTP timeouts. WiFi is torn
  // down when this activity exits. Upstream #3233.
  WiFi.setSleep(false);

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

  // Fetch remote progress. In smart mode, retain the alternate document-id
  // record until both records can be mapped after the Epub is reloaded.
  auto result = KOReaderSyncClient::getProgress(documentHash, remoteProgress);
  LOG_DBG("KOSync", "Primary remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
          matchMethodName(primaryMethod), result, KOReaderSyncClient::lastHttpCode, documentHash.c_str(),
          localProgress.percentage, remoteProgress.percentage, remoteProgress.progress.c_str());

  KOReaderProgress alternateProgress;
  bool hasAlternateProgress = false;
  if (smartSyncEnabled()) {
    const DocumentMatchMethod altMethod = alternateMatchMethod(primaryMethod);
    const std::string altHash = calculateDocumentHashForMethod(epubPath, altMethod);
    if (!altHash.empty() && altHash != documentHash) {
      KOReaderProgress altProgress;
      const auto altResult = KOReaderSyncClient::getProgress(altHash, altProgress);
      LOG_DBG("KOSync", "Alternate remote (%s): result=%d http=%d doc=%s local=%.6f remote=%.6f xpath=%s",
              matchMethodName(altMethod), altResult, KOReaderSyncClient::lastHttpCode, altHash.c_str(),
              localProgress.percentage, altProgress.percentage, altProgress.progress.c_str());

      if (altResult == KOReaderSyncClient::OK) {
        alternateProgress = std::move(altProgress);
        hasAlternateProgress = true;
      }
    }
  }

  if (result == KOReaderSyncClient::NOT_FOUND && hasAlternateProgress) {
    remoteProgress = std::move(alternateProgress);
    hasAlternateProgress = false;
    result = KOReaderSyncClient::OK;
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
      statusMessage = koSyncErrorText(result);
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
      statusMessage = tr(STR_SYNC_BOOK_UNREADABLE);
    }
    requestUpdate(true);
    return;
  }

  const auto mapRemoteProgress = [&](const KOReaderProgress& progress) {
    // The standard KOReader progress XPath is the authoritative content anchor.
    // The CrossPoint server's existing rich page hints remain a legacy fallback.
    const SavedProgressPosition koPos = {progress.progress, progress.percentage};
    CrossPointPosition mapped =
        ProgressMapper::toCrossPoint(*epub, koPos, renderer, localPosition.spineIndex, localPosition.totalPages);
    if (!mapped.hasVisibleTextOffset && progress.position.has_value()) {
      // toCrossPoint above already tried koPos.xpath; if the rich position carries the same XPath,
      // tell fromRichPosition to skip re-resolving it and use its page hints directly.
      const bool sameXPath = progress.position->xpath == progress.progress;
      if (const auto richMapped = ProgressMapper::fromRichPosition(*epub, *progress.position, renderer, sameXPath)) {
        mapped = *richMapped;
      }
    }
    return mapped;
  };

  remotePosition = mapRemoteProgress(remoteProgress);
  if (hasAlternateProgress) {
    const CrossPointPosition alternatePosition = mapRemoteProgress(alternateProgress);
    if (selectRemoteRecord(remotePosition, remoteProgress.percentage, alternatePosition,
                           alternateProgress.percentage) == RemoteRecordChoice::Alternate) {
      remoteProgress = std::move(alternateProgress);
      remotePosition = alternatePosition;
      LOG_DBG("KOSync", "Selected alternate remote record after mapped-position comparison");
    } else {
      LOG_DBG("KOSync", "Kept primary remote record after mapped-position comparison");
    }
  }

  const ProgressComparison comparison =
      compareProgress(localPosition, localProgress.percentage, remotePosition, remoteProgress.percentage);
  if (smartSyncEnabled()) {
    LOG_DBG("KOSync", "Smart decision: doc=%s result=%d local=%.6f remote=%.6f remoteXpath=%s mapped=%d/%d",
            primaryHash.c_str(), static_cast<int>(comparison), localProgress.percentage, remoteProgress.percentage,
            remoteProgress.progress.c_str(), remotePosition.spineIndex, remotePosition.pageNumber);
    switch (comparison) {
      case ProgressComparison::Synchronized:
        completeAlreadySynced();
        return;
      case ProgressComparison::LocalAhead:
        documentHash = primaryHash;
        performUpload();
        return;
      case ProgressComparison::RemoteAhead:
        applyRemoteProgress(remotePosition.spineIndex, remotePosition.pageNumber);
        return;
      case ProgressComparison::Unknown:
        LOG_DBG("KOSync", "Smart sync comparison unknown; opening manual selection");
        break;
    }
  }

  // localProgress was pre-computed in EpubReaderActivity before the Epub was released.
  prepareComparison();
  {
    RenderLock lock(*this);
    state = SHOWING_RESULT;
    setChoiceIndex(comparison == ProgressComparison::LocalAhead ? 1 : 0);
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
    pos.spineIndex = static_cast<uint16_t>(localPosition.spineIndex);
    pos.pageNumber = static_cast<uint16_t>(localPosition.pageNumber);
    pos.totalPages = static_cast<uint16_t>(localPosition.totalPages > 0 ? localPosition.totalPages : 1);
    if (localPosition.hasParagraphIndex) {
      pos.paragraphIndex = localPosition.paragraphIndex;
    }
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
      statusMessage = koSyncErrorText(result);
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
  const std::string localChapter =
      !localChapterName.empty() ? localChapterName
                                : (std::string(tr(STR_SECTION_PREFIX)) + std::to_string(localPosition.spineIndex + 1));

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
  snprintf(buffer, sizeof(buffer), tr(STR_PAGE_TOTAL_OVERALL_FORMAT), localPosition.pageNumber + 1,
           localPosition.totalPages, localProgress.percentage * 100);
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
      view.lines = {state == UPLOAD_COMPLETE ? tr(STR_UPLOAD_SUCCESS)
                    : appliedRemote          ? tr(STR_REMOTE_APPLIED)
                                             : tr(STR_ALREADY_SYNCED),
                    nullptr, nullptr, nullptr};
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
    applyRemoteProgress(remotePosition.spineIndex, remotePosition.pageNumber);
    return;
  }
  performUpload();
}
