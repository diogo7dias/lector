#include "FontDownloadActivity.h"

#include <ArduinoJson.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_rom_crc.h>

#include "MappedInputManager.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

FontDownloadActivity::FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("FontDownload", renderer, mappedInput), fontInstaller_(sdFontSystem.registry()) {}

// --- Lifecycle ---

void FontDownloadActivity::onEnter() {
  Activity::onEnter();
  WiFi.mode(WIFI_STA);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void FontDownloadActivity::onExit() {
  Activity::onExit();

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
    {
      RenderLock lock(*this);
      state_ = ERROR;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state_ = FAMILY_LIST;
    selectedIndex_ = 0;
  }
}

// --- Manifest fetching ---

bool FontDownloadActivity::fetchAndParseManifest() {
  // Download manifest to a temp file on SD card to avoid holding both
  // TLS buffers and the full JSON string in RAM simultaneously.
  static constexpr const char* MANIFEST_TMP = "/fonts_manifest.tmp";

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

  // HTTP client is now closed — TLS buffers freed. Parse JSON from file.
  HalFile manifestFile;
  if (!Storage.openFileForRead("FONT", MANIFEST_TMP, manifestFile)) {
    LOG_ERR("FONT", "Failed to open temp manifest");
    Storage.remove(MANIFEST_TMP);
    errorMessage_ = "Failed to read font list";
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, manifestFile);
  manifestFile.close();
  Storage.remove(MANIFEST_TMP);

  if (err) {
    LOG_ERR("FONT", "Manifest parse error: %s", err.c_str());
    errorMessage_ = "Invalid font manifest";
    return false;
  }

  int version = doc["version"] | 0;
  if (version != FONTS_MANIFEST_VERSION) {
    LOG_ERR("FONT", "Unsupported manifest version: %d", version);
    errorMessage_ = "Unsupported manifest version";
    return false;
  }

  baseUrl_ = doc["baseUrl"] | "";
  families_.clear();
  fontInstaller_.refreshRegistry();

  JsonArray familiesArr = doc["families"].as<JsonArray>();
  families_.reserve(familiesArr.size());

  for (JsonObject fObj : familiesArr) {
    ManifestFamily family;
    family.name = fObj["name"] | "";
    family.description = fObj["description"] | "";

    for (JsonVariant s : fObj["styles"].as<JsonArray>()) {
      family.styles.push_back(s.as<std::string>());
    }

    family.totalSize = 0;
    for (JsonObject fileObj : fObj["files"].as<JsonArray>()) {
      ManifestFile file;
      file.name = fileObj["name"] | "";
      file.size = fileObj["size"] | 0;

      if (!fileObj["crc32"].is<uint32_t>()) {
        LOG_ERR("FONT", "Malformed manifest file entry: missing or invalid crc32 for %s", file.name.c_str());
        errorMessage_ = "Invalid font manifest";
        return false;
      }
      file.crc32 = fileObj["crc32"].as<uint32_t>();

      family.totalSize += file.size;
      family.files.push_back(std::move(file));
    }

    family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());

    // Detect updates by comparing manifest file sizes with files on disk.
    // Not a checksum, but a size mismatch reliably indicates a rebuild in practice.
    if (family.installed) {
      for (const auto& file : family.files) {
        char path[128];
        FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), path, sizeof(path));
        HalFile f;
        if (Storage.openFileForRead("FONT", path, f)) {
          size_t actual = f.fileSize();
          f.close();
          if (actual != file.size) {
            family.hasUpdate = true;
            break;
          }
        } else {
          // File missing on disk but family dir exists — treat as update
          family.hasUpdate = true;
          break;
        }
      }
    }

    families_.push_back(std::move(family));
  }

  LOG_DBG("FONT", "Manifest loaded: %zu families", families_.size());
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
  std::vector<std::string> failed;
  for (auto& family : families_) {
    if (!wanted(family)) continue;
    if (!downloadFamily(family)) {
      if (cancelRequested_) return;
      failed.push_back(family.name);
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
  while (static_cast<int32_t>(until - millis()) > 0) {
    if (WiFi.status() == WL_CONNECTED) return true;
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
    LOG_DBG("FONT", "Already installed, skipping %s", file.name.c_str());
    return true;
  }
  // Anything else sitting at that path is the previous release of this font, or
  // a partial from a run that ended long ago. Either way its bytes are not the
  // head of the body about to be fetched, so they go before the first attempt:
  // only partials this loop creates are resumed.
  Storage.remove(destPath);

  for (int attempt = 1; attempt <= MAX_ATTEMPTS; attempt++) {
    {
      RenderLock lock(*this);
      retryAttempt_ = attempt - 1;
      fileProgress_ = 0;
      fileTotal_ = file.size;
      lastDrawnProgressStep_ = -1;
    }
    requestUpdateAndWait();

    if (!waitForWifi()) {
      if (cancelRequested_) return false;
      errorMessage_ = "Lost WiFi connection";
      LOG_ERR("FONT", "No WiFi for attempt %d of %d for %s", attempt, MAX_ATTEMPTS, file.name.c_str());
      Storage.remove(destPath);
      return false;
    }

    const std::string url = baseUrl_ + file.name;
    const auto result = HttpDownloader::downloadToFile(
        url, destPath,
        [this](size_t downloaded, size_t total) {
          fileProgress_ = downloaded;
          fileTotal_ = total;
          // Cancel is polled on every chunk; only the repaint is rationed.
          mappedInput.update();
          if (mappedInput.isPressed(MappedInputManager::Button::Back) ||
              mappedInput.wasPressed(MappedInputManager::Button::Back)) {
            cancelRequested_ = true;
          }
          const int percent = total > 0 ? static_cast<int>(downloaded * 100 / total) : 0;
          const int step = percent / PROGRESS_STEP_PERCENT;
          if (step == lastDrawnProgressStep_) return;
          lastDrawnProgressStep_ = step;
          requestUpdate(true);
        },
        &cancelRequested_, "", "", /*allowResume=*/true);

    if (result == HttpDownloader::ABORTED || cancelRequested_) {
      // A cancel is an answer, not an interruption to be picked up later: leave
      // no partial behind for the next visit to puzzle over.
      Storage.remove(destPath);
      cancelRequested_ = true;
      return false;
    }

    if (result != HttpDownloader::OK) {
      LOG_ERR("FONT", "Download attempt %d of %d failed for %s (%d)", attempt, MAX_ATTEMPTS, file.name.c_str(), result);
      errorMessage_ = "Download failed: " + file.name;
    } else {
      uint32_t actualCrc = 0;
      if (!computeFileCrc32(destPath, actualCrc)) {
        LOG_ERR("FONT", "Failed to open file for CRC check: %s", destPath);
        errorMessage_ = "Failed to compute checksum: " + file.name;
      } else if (actualCrc != file.crc32) {
        // A body that arrived corrupted is worth fetching again: the manifest
        // checksum is the only thing that separates a bad transfer from a font
        // the renderer would later choke on.
        LOG_ERR("FONT", "CRC32 mismatch for %s: got %08x expected %08x", file.name.c_str(), actualCrc, file.crc32);
        errorMessage_ = "Checksum mismatch: " + file.name;
      } else if (!fontInstaller_.validateCpfontFile(destPath)) {
        LOG_ERR("FONT", "Invalid .cpfont: %s", destPath);
        errorMessage_ = "Invalid font file: " + file.name;
      } else {
        LOG_DBG("FONT", "Downloaded %s (size=%zu crc32=%08x)", file.name.c_str(), file.size, actualCrc);
        RenderLock lock(*this);
        retryAttempt_ = 0;
        return true;
      }
    }

    // A transfer that stopped early leaves bytes the next attempt resumes from,
    // so only a body that arrived whole and wrong is swept away here. Either way
    // nothing usable survives a full give-up, handled below.
    if (result == HttpDownloader::OK) Storage.remove(destPath);
    if (attempt < MAX_ATTEMPTS && !waitBeforeRetry(RETRY_DELAY_MS * static_cast<uint32_t>(attempt))) return false;
  }

  // Out of attempts: a partial left on the card would be read as an installed
  // style on the next visit, so it goes.
  Storage.remove(destPath);
  LOG_ERR("FONT", "Giving up on %s after %d attempts", file.name.c_str(), MAX_ATTEMPTS);
  RenderLock lock(*this);
  retryAttempt_ = 0;
  return false;
}

bool FontDownloadActivity::downloadFamily(ManifestFamily& family) {
  // A failed update must not cost the reader the copy already on the card, so
  // only a family that was not installed before this download is cleared out.
  const bool wasInstalled = family.installed;

  {
    RenderLock lock(*this);
    state_ = DOWNLOADING;
    downloadingFamilyIndex_ = static_cast<int>(&family - families_.data());
    fileProgress_ = 0;
    fileTotal_ = 0;
    retryAttempt_ = 0;
  }
  requestUpdateAndWait();

  if (!fontInstaller_.ensureFamilyDir(family.name.c_str())) {
    errorMessage_ = "Failed to create font directory";
    return false;
  }

  for (const auto& file : family.files) {
    char destPath[128];
    FontInstaller::buildFontPath(family.name.c_str(), file.name.c_str(), destPath, sizeof(destPath));

    if (!downloadFileWithRetries(file, destPath)) {
      if (!wasInstalled) {
        fontInstaller_.deleteFamily(family.name.c_str());
        family.installed = false;
      }
      fontInstaller_.refreshRegistry();
      family.installed = fontInstaller_.isFamilyInstalled(family.name.c_str());
      family.hasUpdate = family.installed;
      if (cancelRequested_) {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      return false;
    }
    currentFileIndex_++;
  }

  fontInstaller_.refreshRegistry();
  family.installed = true;
  family.hasUpdate = false;
  return true;
}

void FontDownloadActivity::downloadSingleFamily(ManifestFamily& family) {
  cancelRequested_ = false;
  if (downloadFamily(family)) {
    RenderLock lock(*this);
    state_ = COMPLETE;
    return;
  }
  if (cancelRequested_) return;
  RenderLock lock(*this);
  state_ = ERROR;
}

void FontDownloadActivity::promptDeleteSelectedFamily() {
  const int pendingDeleteFamilyIndex = familyIndexFromList(selectedIndex_);
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

  auto& family = families_[familyIndexFromList(selectedIndex_)];

  if (fontInstaller_.deleteFamily(family.name.c_str()) != FontInstaller::Error::OK) {
    RenderLock lock(*this);
    state_ = ERROR;
    errorMessage_ = "Failed to delete font";
  } else {
    fontInstaller_.refreshRegistry();
    family.installed = false;
    family.hasUpdate = false;
  }

  requestUpdate();
}

bool FontDownloadActivity::isSelectedFamilyDeletable() const {
  if (isDownloadAllRow(selectedIndex_) || isUpdateAllRow(selectedIndex_)) return false;
  if (selectedIndex_ < specialRowCount() || selectedIndex_ >= listItemCount()) return false;
  const auto& family = families_[familyIndexFromList(selectedIndex_)];
  return family.installed && !family.hasUpdate;
}

// --- Input handling ---

void FontDownloadActivity::loop() {
  if (state_ == FAMILY_LIST) {
    auto activateSelected = [this] {
      if (families_.empty()) return;
      if (isDownloadAllRow(selectedIndex_)) {
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& f : families_) {
          if (!f.installed) currentFileTotal_ += f.files.size();
        }
        downloadAll();
      } else if (isUpdateAllRow(selectedIndex_)) {
        currentFileIndex_ = 0;
        currentFileTotal_ = 0;
        for (const auto& f : families_) {
          if (f.hasUpdate) currentFileTotal_ += f.files.size();
        }
        updateAll();
      } else {
        auto& family = families_[familyIndexFromList(selectedIndex_)];
        if (!family.installed || family.hasUpdate) {
          currentFileIndex_ = 0;
          currentFileTotal_ = family.files.size();
          downloadSingleFamily(family);
        } else {
          promptDeleteSelectedFamily();
          return;
        }
      }
      requestUpdateAndWait();
    };

    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finish();
      return;
    }

    const int listSize = listItemCount();
    const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);

    buttonNavigator_.onNextStep([this, listSize] {
      selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onPreviousStep([this, listSize] {
      selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, listSize);
      requestUpdate();
    });

    buttonNavigator_.onNextContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::nextPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    buttonNavigator_.onPreviousContinuous([this, listSize, pageItems] {
      selectedIndex_ = ButtonNavigator::previousPageIndex(selectedIndex_, listSize, pageItems);
      requestUpdate();
    });

    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      activateSelected();
      return;
    }
  } else if (state_ == COMPLETE) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
        mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      requestUpdate();
    }
  } else if (state_ == ERROR) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      {
        RenderLock lock(*this);
        state_ = FAMILY_LIST;
      }
      requestUpdate();
    } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      if (downloadingFamilyIndex_ >= 0 && downloadingFamilyIndex_ < static_cast<int>(families_.size())) {
        currentFileIndex_ = 0;
        currentFileTotal_ = families_[downloadingFamilyIndex_].files.size();
        downloadSingleFamily(families_[downloadingFamilyIndex_]);
        requestUpdateAndWait();
        return;
      } else {
        {
          RenderLock lock(*this);
          state_ = FAMILY_LIST;
        }
        requestUpdate();
      }
    }
  }
}

// --- Rendering ---

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

void FontDownloadActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_FONT_BROWSER));

  const auto lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const auto contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const auto centerY = (pageHeight - lineHeight) / 2;

  if (state_ == LOADING_MANIFEST) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_LOADING_FONT_LIST));
  } else if (state_ == FAMILY_LIST) {
    if (families_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_NO_FONTS_AVAILABLE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    } else {
      GUI.drawList(
          renderer,
          Rect{0, contentTop, pageWidth, pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing},
          listItemCount(), selectedIndex_,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index)) {
              return std::string(tr(STR_DOWNLOAD_ALL)) + " (" + formatSize(totalDownloadSize()) + ")";
            }
            if (isUpdateAllRow(index)) {
              return std::string(tr(STR_UPDATE_ALL)) + " (" + formatSize(totalUpdateSize()) + ")";
            }
            return families_[familyIndexFromList(index)].name;
          },
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            return families_[familyIndexFromList(index)].description;
          },
          nullptr,
          [this](int index) -> std::string {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return "";
            const auto& f = families_[familyIndexFromList(index)];
            if (f.hasUpdate) return tr(STR_UPDATE_AVAILABLE);
            if (f.installed) return tr(STR_INSTALLED);
            return "";
          },
          true,
          [this](int index) -> bool {
            if (isDownloadAllRow(index) || isUpdateAllRow(index)) return false;
            const auto& f = families_[familyIndexFromList(index)];
            return f.installed && !f.hasUpdate;
          });

      const auto labels = mappedInput.mapLabels(tr(STR_BACK),
                                                isSelectedFamilyDeletable()      ? tr(STR_DELETE)
                                                : isUpdateAllRow(selectedIndex_) ? tr(STR_UPDATE)
                                                                                 : tr(STR_DOWNLOAD),
                                                tr(STR_DIR_UP), tr(STR_DIR_DOWN));
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    }
  } else if (state_ == DOWNLOADING) {
    const auto& family = families_[downloadingFamilyIndex_];

    std::string statusText = std::string(tr(STR_DOWNLOADING)) + " " + family.name + " (" +
                             std::to_string(currentFileIndex_ + 1) + "/" + std::to_string(currentFileTotal_) + ")";
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, statusText.c_str());

    float progress = 0;
    if (fileTotal_ > 0) {
      progress = static_cast<float>(fileProgress_) / static_cast<float>(fileTotal_);
    }

    int barY = centerY + metrics.verticalSpacing;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, pageWidth - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<int>(progress * 100), 100);

    if (retryAttempt_ > 0) {
      const std::string retryText =
          std::string(tr(STR_RETRY)) + " " + std::to_string(retryAttempt_ + 1) + "/" + std::to_string(MAX_ATTEMPTS);
      // Two gaps below the bar, not one: at one gap the line sits close enough to the
      // bar's outline to read as touching it.
      renderer.drawCenteredText(UI_10_FONT_ID, barY + metrics.progressBarHeight + metrics.verticalSpacing * 2,
                                retryText.c_str());
    }

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == COMPLETE) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY, tr(STR_FONT_INSTALLED), true, EpdFontFamily::REGULAR);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state_ == ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight, tr(STR_FONT_INSTALL_FAILED), true,
                              EpdFontFamily::REGULAR);
    if (!errorMessage_.empty()) {
      renderer.drawCenteredText(UI_10_FONT_ID, centerY + metrics.verticalSpacing, errorMessage_.c_str());
    }
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_RETRY), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  // A differential pass leaves the previous screen showing through as grey residue,
  // which is why the list used to sit under the progress bar for the whole download.
  // The screen changes wholesale between states and at each new file, so those paints
  // take a cleanup pass; the bar's own steps stay differential.
  const bool screenChanged = state_ != lastDisplayedState_ || downloadingFamilyIndex_ != lastDisplayedFamilyIndex_ ||
                             currentFileIndex_ != lastDisplayedFileIndex_ || retryAttempt_ != lastDisplayedRetry_;
  lastDisplayedState_ = state_;
  lastDisplayedFamilyIndex_ = downloadingFamilyIndex_;
  lastDisplayedFileIndex_ = currentFileIndex_;
  lastDisplayedRetry_ = retryAttempt_;

  renderer.displayBuffer(screenChanged ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
}
