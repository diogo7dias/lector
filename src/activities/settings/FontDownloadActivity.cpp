#include "FontDownloadActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "Memory.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/TlsScratchHeap.h"

namespace {

// Downloaded once per visit and kept until onExit: a font download re-reads one
// family's file names from it rather than holding every family's names in RAM.
constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

}  // namespace

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : UiStatusActivity("FontDownload", renderer, mappedInput), fontInstaller_(sdFontSystem.registry()) {}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  UiStatusActivity::onEnter();

  // Heap-critical transition, the same one CrossPointWebServerActivity and
  // CalibreConnectActivity already guard against: WiFi takes ~45 KB, and the
  // manifest download then needs two TLS handshakes, because GitHub redirects
  // release downloads to a second host. On a reader with a large SD font
  // resident, the second handshake failed inside wolfSSL's big-integer maths
  // (MP_EXPTMOD_E / PEER_KEY_ERROR) with 33908 bytes free and a 30708-byte
  // largest block. These caches rebuild on demand; the download cannot.
  if (auto* fcm = renderer.getFontCacheManager()) {
    LOG_DBG("FONT", "Free heap before SD font cache release: %d bytes", ESP.getFreeHeap());
    fcm->releaseSdFontCaches();
    LOG_DBG("FONT", "Free heap after SD font cache release: %d bytes", ESP.getFreeHeap());
  }

  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

  // The manifest was kept on the card so a download could re-read one family's
  // file names without holding all of them in RAM. Nothing needs it now.
  Storage.remove(MANIFEST_TMP);

  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void FontDownloadActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state_ = LOADING_MANIFEST;
  }
  requestUpdateAndWait();

  if (!fetchAndParseManifest()) {
    // Back pressed while waiting for the link is an answer, not a failure to
    // report: leave the screen rather than accuse the server of anything.
    if (cancelRequested_) {
      finish();
      return;
    }
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
  }
  refreshRows();
  setListSelection(0);
}

// --- Manifest fetching ---

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.

  // The font list is the first thing a reader hits after joining WiFi, and a
  // handshake that fails while the connection settles used to end the trip here.
  auto result = HttpDownloader::OK;
  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    if (!waitForWifi()) {
      result = HttpDownloader::HTTP_ERROR;
      break;
    }

    result = HttpDownloader::downloadToFile(FONT_MANIFEST_URL, MANIFEST_TMP, nullptr);
    if (result == HttpDownloader::OK) break;
    LOG_ERR("FONT", "Manifest fetch attempt %d of %d failed (%d)", attempt, MAX_ATTEMPTS, result);
    if (attempt < MAX_ATTEMPTS) waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt));
  }
  if (result != HttpDownloader::OK) {
    LOG_ERR("FONT", "Failed to fetch manifest from %s", FONT_MANIFEST_URL);
    errorMessage_ = "Failed to fetch font list";
    Storage.remove(MANIFEST_TMP);
    return false;
  }

  // HTTP client is now closed — TLS buffers freed. The manifest is ~37 KB and a DOM
  // of it needs upwards of 60 KB contiguous, which the heap cannot spare with WiFi up:
  // that allocation is what threw bad_alloc and aborted the firmware. Stream it instead.
  //
  // The file stays on the card afterwards. Holding all 26 families' file names costs
  // 27456 bytes, and a font download then has to open a TLS session on what is left:
  // that failed with 4844 bytes free. So the names are re-read for one family at a
  // time, straight from this file, and onExit removes it.
  if (!parseManifest(FontManifestParser::FileRetention::None, nullptr, families_)) return false;

  fontInstaller_.refreshRegistry();

  LOG_DBG("FONT", "Manifest loaded: %zu families", families_.size());
  return true;
}

// Runs as each family finishes parsing, while its file names are still in hand.
// Whether the family is installed, and whether the copy on the card is stale, can
// only be answered from those names, and a moment later they are gone.
void FontDownloadActivity::stampDiskState(void* context, ManifestFamily& family, const ManifestFile* files,
                                          const size_t count) {
  auto* self = static_cast<FontDownloadActivity*>(context);
  family.installed = self->fontInstaller_.isFamilyInstalled(family.name);
  family.hasUpdate = false;
  if (!family.installed) return;

  // Detect updates by comparing manifest file sizes with files on disk. Not a
  // checksum, but a size mismatch reliably indicates a rebuild in practice.
  for (size_t i = 0; i < count; i++) {
    const ManifestFile& file = files[i];
    char path[128];
    FontInstaller::buildFontPath(family.name, file.name, path, sizeof(path));
    HalFile f;
    if (Storage.openFileForRead("FONT", path, f)) {
      const size_t actual = f.fileSize();
      f.close();
      if (actual != file.size) {
        family.hasUpdate = true;
        return;
      }
    } else {
      // File missing on disk but family dir exists — treat as update
      family.hasUpdate = true;
      return;
    }
  }
}

// Streams the manifest already on the card. `retention` decides whose file names
// survive the parse; `retainFor` names that family when retention is One.
bool FontDownloadActivity::parseManifest(const FontManifestParser::FileRetention retention, const char* retainFor,
                                         std::vector<ManifestFamily>& out) {
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    errorMessage_ = "Failed to read font list";
    return false;
  }

  out.clear();
  out.shrink_to_fit();

  // On the heap, not the stack: the parser carries a fixed file scratch array and
  // an activity task's stack is not the place for it.
  auto parserOwner = makeUniqueNoThrow<FontManifestParser>();
  if (!parserOwner) {
    manifestFile.close();
    LOG_ERR("FONT", "No room for the manifest parser");
    errorMessage_ = "Not enough memory for font list";
    return false;
  }
  FontManifestParser& parser = *parserOwner;
  parser.retainFiles(retention);
  if (retention == FontManifestParser::FileRetention::One && retainFor) parser.retainFilesFor(retainFor);
  // Only the first pass cares about the card: a re-read for one family is answering
  // "which files", not "what is installed", and that is already known by then.
  if (retention == FontManifestParser::FileRetention::None) parser.setFamilyHook(&stampDiskState, this);

  {
    auto buffer = makeUniqueNoThrow<char[]>(MANIFEST_CHUNK);
    if (!buffer) {
      manifestFile.close();
      LOG_ERR("FONT", "No room for the manifest read buffer");
      errorMessage_ = "Not enough memory for font list";
      return false;
    }
    while (true) {
      const int got = manifestFile.read(buffer.get(), MANIFEST_CHUNK);
      if (got <= 0) break;
      parser.feed(buffer.get(), static_cast<size_t>(got));
      if (parser.hasError()) break;
    }
  }
  manifestFile.close();
  parser.finish();

  LOG_DBG("FONT", "Manifest parsed: free %d bytes, largest block %d bytes", ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());

  if (parser.hasError()) {
    if (parser.outOfMemory()) {
      LOG_ERR("FONT", "Out of memory while reading the font manifest (free %d bytes, largest block %d bytes)",
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      errorMessage_ = "Not enough memory for font list";
    } else if (parser.tooLarge()) {
      LOG_ERR("FONT", "Manifest exceeds the %u family / %u file limits",
              static_cast<unsigned>(FontManifestParser::MAX_FAMILIES),
              static_cast<unsigned>(FontManifestParser::MAX_FILES_PER_FAMILY));
      errorMessage_ = "Font list too large";
    } else {
      LOG_ERR("FONT", "Manifest parse error");
      errorMessage_ = "Invalid font manifest";
    }
    return false;
  }

  if (parser.version() != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", parser.version());
    errorMessage_ = "Unsupported manifest version";
    return false;
  }

  baseUrl_ = parser.baseUrl();
  out = std::move(parser.families());
  return true;
}

// --- Download ---

void FontDownloadActivity::downloadAll() {
  cancelRequested_ = false;
  runBatch([](const ManifestFamily& family) { return !family.installed; });
}

void FontDownloadActivity::updateAll() {
  cancelRequested_ = false;
  runBatch([](const ManifestFamily& family) { return family.hasUpdate; });
}

void FontDownloadActivity::runBatch(const std::function<bool(const ManifestFamily&)>& wanted) {
  // One family that cannot be fetched no longer abandons the rest: the others are
  // independent downloads, and stopping at the first failure meant a single flaky
  // file cost the reader every font behind it in the list.
  // Names, not references: downloadFamily() releases families_ while a transfer
  // is running and rebuilds it afterwards, so any pointer into it goes stale.
  std::vector<std::string> queued;
  for (const auto& family : families_) {
    if (wanted(family)) queued.emplace_back(family.name);
  }

  std::vector<std::string> failed;
  for (const auto& name : queued) {
    if (!downloadFamily(name)) {
      if (cancelRequested_) return;
      failed.push_back(name);
    }
    if (cancelRequested_) return;
  }

  RenderLock lock(*this);
  if (failed.empty()) {
    state_ = COMPLETE;
    return;
  }
  state_ = ERROR;
  errorMessage_ = "Could not install: " + failed.front();
  for (size_t i = 1; i < failed.size(); i++) errorMessage_ += ", " + failed[i];
}

bool FontDownloadActivity::showDownloadAllRow() const {
  for (const auto& f : families_) {
    if (!f.installed) return true;
  }
  return false;
}

bool FontDownloadActivity::showUpdateAllRow() const {
  for (const auto& f : families_) {
    if (f.hasUpdate) return true;
  }
  return false;
}

int FontDownloadActivity::specialRowCount() const {
  return (showDownloadAllRow() ? 1 : 0) + (showUpdateAllRow() ? 1 : 0);
}

bool FontDownloadActivity::isDownloadAllRow(int index) const { return showDownloadAllRow() && index == 0; }

bool FontDownloadActivity::isUpdateAllRow(int index) const {
  return showUpdateAllRow() && index == (showDownloadAllRow() ? 1 : 0);
}

int FontDownloadActivity::listItemCount() const {
  return families_.empty() ? 0 : static_cast<int>(families_.size()) + specialRowCount();
}

size_t FontDownloadActivity::totalDownloadSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (!f.installed) total += f.totalSize;
  }
  return total;
}

size_t FontDownloadActivity::totalUpdateSize() const {
  size_t total = 0;
  for (const auto& f : families_) {
    if (f.hasUpdate) total += f.totalSize;
  }
  return total;
}

// Standard CRC32 matching zlib/Python zlib.crc32().
size_t FontDownloadActivity::stagedSize(const char* path) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) return 0;
  return f.fileSize();
}

bool FontDownloadActivity::computeFileCrc32(const char* path, uint32_t& outCrc) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", path, f)) {
    return false;
  }
  constexpr size_t BUF_SIZE = 128;
  uint8_t buf[BUF_SIZE];
  uint32_t crc = 0;
  while (f.available()) {
    const int n = f.read(buf, BUF_SIZE);
    if (n <= 0) break;
    crc = esp_rom_crc32_le(crc, buf, static_cast<uint32_t>(n));
  }
  outCrc = crc;
  return true;
}

bool FontDownloadActivity::waitBeforeRetry(const uint32_t ms) {
  const uint32_t until = millis() + ms;
  while (static_cast<int32_t>(until - millis()) > 0) {
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested_ = true;
      return false;
    }
    delay(20);
  }
  return true;
}

bool FontDownloadActivity::waitForWifi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  LOG_DBG("FONT", "Waiting for WiFi to come back");
  WiFi.reconnect();
  const uint32_t until = millis() + WIFI_WAIT_MS;
  uint32_t nextReconnect = millis() + RECONNECT_EVERY_MS;
  while (static_cast<int32_t>(until - millis()) > 0) {
    if (WiFi.status() == WL_CONNECTED) return true;
    // One reconnect() at the top is not enough: the first can be issued while
    // the radio is still tearing the old association down, and is then dropped.
    if (static_cast<int32_t>(nextReconnect - millis()) <= 0) {
      WiFi.reconnect();
      nextReconnect = millis() + RECONNECT_EVERY_MS;
    }
    mappedInput.update();
    if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      cancelRequested_ = true;
      return false;
    }
    delay(100);
  }
  LOG_ERR("FONT", "WiFi did not come back within %u ms", WIFI_WAIT_MS);
  return false;
}

bool FontDownloadActivity::fileAlreadyInstalled(const ManifestFile& file, const char* destPath) {
  HalFile f;
  if (!Storage.openFileForRead("FONT", destPath, f)) return false;
  const size_t actual = f.fileSize();
  f.close();
  if (actual != file.size) return false;
  uint32_t crc = 0;
  if (!computeFileCrc32(destPath, crc)) return false;
  return crc == file.crc32;
}

bool FontDownloadActivity::downloadFileWithRetries(const ManifestFile& file, const char* destPath) {
  // A file that survived an earlier run is not fetched again, so retrying a
  // batch that died halfway picks up where it stopped instead of paying for
  // every megabyte a second time.
  if (fileAlreadyInstalled(file, destPath)) {
    LOG_DBG("FONT", "Already installed, skipping %s", file.name);
    return true;
  }

  // Everything lands in a staging file and only takes the real name once it has
  // passed its checksum. Two things follow: the copy already on the card
  // survives a failed update untouched, and a partial can never be discovered as
  // a font, because the registry only accepts names ending in ".cpfont".
  char partPath[192];
  snprintf(partPath, sizeof(partPath), "%s.part", destPath);
  // Bytes left by an older run belong to an older release of this file, so they
  // are not the head of the body about to arrive: only partials this loop
  // creates are ever resumed.
  Storage.remove(partPath);

  // An attempt that moved the partial forward is not a wasted attempt. A body
  // that dies part-way is resumed from the bytes already staged, so five failures
  // that each carry 15 KB are progress, while five that carry nothing are not.
  // The budget below therefore counts only the fruitless ones, and MAX_TOTAL_ATTEMPTS
  // bounds how long a file that is crawling is allowed to keep crawling.
  size_t stagedBefore = 0;
  int fruitless = 0;
  for (int total = 1, attempt = 1; fruitless < MAX_ATTEMPTS && total <= MAX_TOTAL_ATTEMPTS; total++, attempt++) {
    {
      RenderLock lock(*this);
      retryAttempt_ = attempt - 1;
      fileProgress_ = 0;
      fileTotal_ = file.size;
      lastDrawnProgressStep_ = -1;
      refreshProgressLines();
    }
    requestUpdateAndWait();

    // A link that has not come back yet costs an attempt rather than the file:
    // the bytes already staged stay, and the next attempt resumes from them.
    if (!waitForWifi()) {
      if (cancelRequested_) {
        Storage.remove(partPath);
        return false;
      }
      LOG_ERR("FONT", "No WiFi for attempt %d for %s", total, file.name);
      errorMessage_ = "Lost WiFi connection";
      fruitless++;
      if (fruitless < MAX_ATTEMPTS && total < MAX_TOTAL_ATTEMPTS &&
          !waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt))) {
        Storage.remove(partPath);
        return false;
      }
      continue;
    }

    LOG_DBG("FONT", "Fetching %s: free %d bytes, largest block %d bytes", file.name, ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());

    // A handshake started on a nearly empty heap does not fail cleanly: wolfSSL
    // spent 60 seconds inside its retry with 1004 bytes free and the reader was
    // unresponsive until the watchdog reset it. Refuse the attempt instead.
    if (ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
      LOG_ERR("FONT", "Only %d bytes free, need %d for a secure connection", ESP.getFreeHeap(), MIN_HEAP_FOR_TLS);
      errorMessage_ = "Not enough memory to download fonts";
      return false;
    }

    const std::string url = baseUrl_ + file.name;
    // The framebuffer's 48 KB go to wolfSSL for the length of the transfer, which
    // is the only place the reader has the room for a 16 KB TLS record buffer: with
    // WiFi up the heap runs out a few kilobytes short, every time, at any level of
    // fragmentation. Nothing may draw while the bytes are lent, so the progress bar
    // holds still until the file lands and the panel keeps the screen drawn above.
    drawingSuspended_ = true;
    HttpDownloader::DownloadError result;
    {
      GfxRenderer::FrameBufferLoan loan(renderer);
      const tls_scratch::Session tlsScratch;
      if (!tlsScratch.active()) {
        LOG_ERR("FONT", "Framebuffer not lent; the transfer runs on the heap alone");
      }
      result = HttpDownloader::downloadToFile(
        url, partPath,
        [this](size_t downloaded, size_t total) {
          fileProgress_ = downloaded;
          fileTotal_ = total;
          // Cancel is polled on every chunk; only the repaint is rationed.
          mappedInput.update();
          if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
              mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            cancelRequested_ = true;
          }
          if (drawingSuspended_) return;
          const int percent = total > 0 ? static_cast<int>(downloaded * 100 / total) : 0;
          const int step = percent / PROGRESS_STEP_PERCENT;
          if (step == lastDrawnProgressStep_) return;
          lastDrawnProgressStep_ = step;
          requestUpdate(true);
        },
        &cancelRequested_, "", "", /*allowResume=*/true);
    }
    drawingSuspended_ = false;
    // The loan hands the framebuffer back white, so the next paint has to be a
    // full one rather than a difference against a screen that is no longer there.
    lastDisplayedState_ = WIFI_SELECTION;
    requestUpdateAndWait();

    if (result == HttpDownloader::ABORTED || cancelRequested_) {
      // A cancel is an answer, not an interruption to be picked up later: leave
      // no staging file behind for the next visit to puzzle over.
      Storage.remove(partPath);
      cancelRequested_ = true;
      return false;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT", "Download attempt %d failed for %s (%d)", total, file.name, result);
      errorMessage_ = std::string("Download failed: ") + file.name;
    } else {
      uint32_t actualCrc = 0;
      if (!computeFileCrc32(partPath, actualCrc)) {
        LOG_ERR("FONT", "Failed to open file for CRC check: %s", partPath);
        errorMessage_ = std::string("Failed to compute checksum: ") + file.name;
      } else if (actualCrc != file.crc32) {
        // A body that arrived corrupted is worth fetching again: the manifest
        // checksum is the only thing that separates a bad transfer from a font
        // the renderer would later choke on.
        LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", file.name, actualCrc, file.crc32);
        errorMessage_ = std::string("Checksum mismatch: ") + file.name;
      } else if (!fontInstaller_.validateCpfontFile(partPath)) {
        LOG_ERR("FONT", "Invalid .cpfont: %s", partPath);
        errorMessage_ = std::string("Invalid font file: ") + file.name;
      } else if (!promoteStagedFile(partPath, destPath)) {
        errorMessage_ = std::string("Failed to install: ") + file.name;
      } else {
        LOG_DBG("FONT", "Downloaded %s (size=%u crc32=%08x)", file.name, static_cast<unsigned>(file.size), actualCrc);
        RenderLock lock(*this);
        retryAttempt_ = 0;
        return true;
      }
    }

    // A transfer that stopped early leaves bytes the next attempt resumes from,
    // so only a body that arrived whole and wrong is swept away here.
    if (result == HttpDownloader::OK) Storage.remove(partPath);

    const size_t stagedNow = stagedSize(partPath);
    if (stagedNow > stagedBefore) {
      LOG_DBG("FONT", "Attempt %d carried %u bytes, %u staged of %u", total,
              static_cast<unsigned>(stagedNow - stagedBefore), static_cast<unsigned>(stagedNow),
              static_cast<unsigned>(file.size));
      fruitless = 0;
      attempt = 0;
    } else {
      fruitless++;
    }
    stagedBefore = stagedNow;

    if (fruitless < MAX_ATTEMPTS && total < MAX_TOTAL_ATTEMPTS &&
        !waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt + 1))) {
      Storage.remove(partPath);
      return false;
    }
  }

  Storage.remove(partPath);
  LOG_ERR("FONT", "Giving up on %s after %d fruitless attempts", file.name, MAX_ATTEMPTS);
  RenderLock lock(*this);
  retryAttempt_ = 0;
  return false;
}

bool FontDownloadActivity::promoteStagedFile(const char* partPath, const char* destPath) {
  // The old copy goes only now, with a verified replacement in hand.
  if (Storage.exists(destPath) && !Storage.remove(destPath)) {
    LOG_ERR("FONT", "Failed to remove the previous file: %s", destPath);
    return false;
  }
  if (!Storage.rename(partPath, destPath)) {
    LOG_ERR("FONT", "Failed to rename %s to %s", partPath, destPath);
    return false;
  }
  return true;
}

bool FontDownloadActivity::downloadFamily(const std::string& familyName) {
  // A failed update must not cost the reader the copy already on the card, so
  // only a family that was not installed before this download is cleared out.
  bool wasInstalled = false;
  int familyIndex = -1;
  for (size_t i = 0; i < families_.size(); i++) {
    if (familyName == families_[i].name) {
      wasInstalled = families_[i].installed;
      familyIndex = static_cast<int>(i);
      break;
    }
  }
  if (familyIndex < 0) {
    LOG_ERR("FONT", "No family named %s in the manifest", familyName.c_str());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = familyIndex;
    downloadingFamilyName_ = familyName;
    fileProgress_ = 0;
    fileTotal_ = 0;
    retryAttempt_ = 0;
    refreshProgressLines();
  }
  requestUpdateAndWait();

  // Drawing the family list reloaded the SD font caches that onEnter had just
  // released: 13216 bytes on this reader, taken back from the heap the per-file
  // TLS session needs. They are released again here, on every family, because
  // every repaint since the last download may have rebuilt them.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->releaseSdFontCaches();
    LOG_DBG("FONT", "Free heap after SD font cache release: %d bytes, largest block %d bytes", ESP.getFreeHeap(),
            ESP.getMaxAllocHeap());
  }

  if (!fontInstaller_.ensureFamilyDir(familyName.c_str())) {
    errorMessage_ = "Failed to create font directory";
    return false;
  }

  // The file names for this one family, read back from the manifest still on the
  // card. Everything else keeps only its counts, which is what leaves room for the
  // TLS session each file needs.
  std::vector<ManifestFamily> reread;
  if (!parseManifest(FontManifestParser::FileRetention::One, familyName.c_str(), reread)) return false;
  std::vector<ManifestFile> files;
  for (auto& candidate : reread) {
    if (familyName == candidate.name) {
      files = std::move(candidate.files);
      break;
    }
  }
  reread.clear();
  reread.shrink_to_fit();
  if (files.empty()) {
    LOG_ERR("FONT", "No files listed for %s in the manifest", familyName.c_str());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  // The list of families is not needed again until the download screen is gone,
  // and the transfer needs every byte it was holding. GitHub's asset host ignores
  // the 2 KB max_fragment_length the reader asks for, so wolfSSL sizes its buffers
  // to 16 KB records mid-body: a transfer that began with 39812 bytes free died at
  // 32768 bytes with 12480 free, while the manifest's own transfer succeeded from
  // 48384. Releasing the list here hands that difference back. reloadFamilies()
  // rebuilds it from the copy still on the card once the files are in.
  std::vector<ManifestFamily>().swap(families_);
  // The rows point into the list that was just released, and the failure paths
  // below can reach the list screen again without rebuilding them.
  rows_.clear();
  rowLabels_.clear();
  rowValues_.clear();

  bool ok = true;
  for (const auto& file : files) {
    char destPath[128];
    FontInstaller::buildFontPath(familyName.c_str(), file.name, destPath, sizeof(destPath));

    if (!downloadFileWithRetries(file, destPath)) {
      ok = false;
      break;
    }
    currentFileIndex_++;
    refreshProgressLines();
  }

  fontInstaller_.refreshRegistry();
  if (!ok) {
    // The files that did land stay: a reader on a poor link gets the family a
    // few styles at a time across runs, and the size check in the manifest
    // marks what is still missing as an update. Only a family that arrived
    // with nothing at all is cleared, so no empty directory is left behind.
    if (!wasInstalled && !fontInstaller_.isFamilyInstalled(familyName.c_str())) {
      fontInstaller_.deleteFamily(familyName.c_str());
      fontInstaller_.refreshRegistry();
    }
  }

  // Rebuilt from the card, so installed and hasUpdate come back stamped from what
  // is actually there now rather than from what this function believes it wrote.
  if (!reloadFamilies()) {
    refreshRows();
    return false;
  }
  refreshRows();

  if (!ok && cancelRequested_) {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
  }
  return ok;
}

bool FontDownloadActivity::reloadFamilies() {
  if (!families_.empty()) return true;
  return parseManifest(FontManifestParser::FileRetention::None, nullptr, families_);
}

void FontDownloadActivity::downloadSingleFamily(const std::string& familyName) {
  cancelRequested_ = false;
  if (downloadFamily(familyName)) {
    RenderLock lock(*this);
    state_ = COMPLETE;
    return;
  }
  if (cancelRequested_) return;
  RenderLock lock(*this);
  state_ = ERROR;
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(listSelection());
  if (pendingDeleteFamilyIndex < 0 || pendingDeleteFamilyIndex >= static_cast<int>(families_.size())) {
    return;
  }

  std::string heading = tr(STR_DELETE);
  const auto& family = families_[pendingDeleteFamilyIndex];
  std::string body = family.name;
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                         [this](const ActivityResult& result) { onDeleteConfirmationResult(result); });
}

void FontDownloadActivity::onDeleteConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    requestUpdate();
    return;
  }

  auto& family = families_[familyIndexFromList(listSelection())];

  if (fontInstaller_.deleteFamily(family.name) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
  }
  // The deleted family drops its "Installed" mark, and the Download-all row may
  // appear now that something is missing again.
  refreshRows();

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(listSelection()) || isUpdateAllRow(listSelection())) return false;
  if (listSelection() < specialRowCount() || listSelection() >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(listSelection())];
  return family.installed && !family.hasUpdate;
}


// --- Rows and lines ---

std::string FontDownloadActivity::formatSize(size_t bytes) {
  char buf[32];
  if (bytes >= 1024 * 1024) {
    snprintf(buf, sizeof(buf), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
  } else if (bytes >= 1024) {
    snprintf(buf, sizeof(buf), "%.0f KB", static_cast<double>(bytes) / 1024.0);
  } else {
    snprintf(buf, sizeof(buf), "%zu B", bytes);
  }
  return buf;
}

void FontDownloadActivity::refreshRows() {
  const int count = listItemCount();
  rowLabels_.clear();
  rowValues_.clear();
  rows_.clear();
  if (count <= 0) return;

  rowLabels_.reserve(count);
  rowValues_.reserve(count);
  for (int index = 0; index < count; ++index) {
    if (isDownloadAllRow(index)) {
      rowLabels_.push_back(std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")");
      rowValues_.emplace_back();
      continue;
    }
    if (isUpdateAllRow(index)) {
      rowLabels_.push_back(std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")");
      rowValues_.emplace_back();
      continue;
    }
    const auto& family = families_[familyIndexFromList(index)];
    rowLabels_.push_back(family.name);
    rowValues_.push_back(family.hasUpdate ? tr(STR_UPDATE_AVAILABLE) : family.installed ? tr(STR_INSTALLED) : "");
  }

  // Second pass: the strings must stop moving before their addresses are taken.
  rows_.resize(count);
  for (int index = 0; index < count; ++index) {
    rows_[index] = freeink::ui::ListItem{};
    rows_[index].label = rowLabels_[index].c_str();
    rows_[index].actionValue = static_cast<int16_t>(index);
    if (!rowValues_[index].empty()) rows_[index].value = rowValues_[index].c_str();
    if (isDownloadAllRow(index) || isUpdateAllRow(index)) continue;
    const auto& family = families_[familyIndexFromList(index)];
    if (family.description[0] != '\0') rows_[index].subtitle = family.description;
  }
}

void FontDownloadActivity::refreshProgressLines() {
  statusLine_ = std::string(tr(STR_DOWNLOADING)) + " " + downloadingFamilyName_ + " (" +
                std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
  // Above the status line rather than below the bar: a retry is context for
  // what is being downloaded, and it reads as that only when it comes first.
  retryLine_ = retryAttempt_ > 0 ? std::string(tr(STR_RETRY)) + " " + std::to_string(retryAttempt_ + 1) + "/" +
                                       std::to_string(MAX_ATTEMPTS)
                                 : std::string();
}

// --- Screen ---

UiStatusActivity::StatusView FontDownloadActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_FONT_BROWSER);
  // A differential pass leaves the previous screen showing through as grey
  // residue, which is why the list used to sit under the progress bar for the
  // whole download. The screen changes wholesale between states and at each new
  // file, so those paints take a cleanup pass; the bar's own steps stay
  // differential.
  const bool screenChanged = state_ != lastDisplayedState_ || downloadingFamilyIndex_ != lastDisplayedFamilyIndex_ ||
                             currentFileIndex_ != lastDisplayedFileIndex_ || retryAttempt_ != lastDisplayedRetry_;
  view.refresh = screenChanged ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH;

  switch (state_) {
    case WIFI_SELECTION:
      // The picker owns the screen.
      view.hidden = true;
      break;
    case LOADING_MANIFEST:
      view.lines = {tr(STR_LOADING_FONT_LIST), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case FAMILY_LIST:
      if (rows_.empty()) {
        view.lines = {tr(STR_NO_FONTS_AVAILABLE), nullptr, nullptr, nullptr};
        break;
      }
      view.listItems = rows_.data();
      view.listCount = static_cast<int>(rows_.size());
      view.listHasSubtitle = true;
      view.confirmHint = isSelectedFamilyDeletable()      ? tr(STR_DELETE)
                         : isUpdateAllRow(listSelection()) ? tr(STR_UPDATE)
                                                           : tr(STR_DOWNLOAD);
      break;
    case DOWNLOADING:
      view.lines = {retryLine_.empty() ? statusLine_.c_str() : retryLine_.c_str(),
                    retryLine_.empty() ? nullptr : statusLine_.c_str(), nullptr, nullptr};
      view.showProgress = true;
      view.progressValue = static_cast<int>(fileProgress_);
      view.progressMax = fileTotal_ > 0 ? static_cast<int>(fileTotal_) : 1;
      view.backHint = tr(STR_CANCEL);
      break;
    case COMPLETE:
      view.lines = {tr(STR_FONT_INSTALLED), nullptr, nullptr, nullptr};
      break;
    case ERROR:
      view.lines = {tr(STR_FONT_INSTALL_FAILED), errorMessage_.empty() ? nullptr : errorMessage_.c_str(), nullptr,
                    nullptr};
      view.confirmHint = tr(STR_RETRY);
      break;
  }
  return view;
}

void FontDownloadActivity::afterRender() {
  lastDisplayedState_ = state_;
  lastDisplayedFamilyIndex_ = downloadingFamilyIndex_;
  lastDisplayedFileIndex_ = currentFileIndex_;
  lastDisplayedRetry_ = retryAttempt_;
}

// --- Input handling ---

void FontDownloadActivity::onListActivated(const int index) {
  if (state_ != FAMILY_LIST || families_.empty()) return;
  setListSelection(index);

  if (isDownloadAllRow(index)) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const auto& f : families_) {
      if (!f.installed) currentFileTotal_ += f.fileCount;
    }
    downloadAll();
  } else if (isUpdateAllRow(index)) {
    currentFileIndex_ = 0;
    currentFileTotal_ = 0;
    for (const auto& f : families_) {
      if (f.hasUpdate) currentFileTotal_ += f.fileCount;
    }
    updateAll();
  } else {
    const auto& family = families_[familyIndexFromList(index)];
    if (!family.installed || family.hasUpdate) {
      currentFileIndex_ = 0;
      currentFileTotal_ = family.fileCount;
      // Copied before the call: the list it points into is released mid-download.
      downloadSingleFamily(std::string(family.name));
    } else {
      promptDeleteSelectedFamily();
      return;
    }
  }
  requestUpdateAndWait();
}

void FontDownloadActivity::onBackButton() {
  // The result screens go back to the list; the list itself leaves.
  if (state_ == COMPLETE || state_ == ERROR) {
    {
      RenderLock lock(*this);
      state_ = FAMILY_LIST;
    }
    requestUpdate();
    return;
  }
  finish();
}

void FontDownloadActivity::onConfirmButton() {
  if (state_ == COMPLETE) {
    onBackButton();
    return;
  }
  if (state_ != ERROR) return;

  // Retry the family that failed, when it is still one the manifest offers.
  const auto retry = std::find_if(families_.begin(), families_.end(),
                                  [this](const ManifestFamily& f) { return downloadingFamilyName_ == f.name; });
  if (retry == families_.end()) {
    onBackButton();
    return;
  }
  currentFileIndex_ = 0;
  currentFileTotal_ = retry->fileCount;
  downloadSingleFamily(downloadingFamilyName_);
  requestUpdateAndWait();
}

// Nothing on this screen answers while the manifest or a file is in flight: the
// download blocks this task and polls for a cancel itself.
bool FontDownloadActivity::handleCustomInput() {
  return state_ == WIFI_SELECTION || state_ == LOADING_MANIFEST || state_ == DOWNLOADING;
}
