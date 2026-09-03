#include "OtaUpdateActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"
#include "network/OtaUpdater.h"
#include "network/TlsScratchHeap.h"

void OtaUpdateActivity::onWifiSelectionComplete(const bool success) {
  if (!success) {
    LOG_ERR("OTA", "WiFi connection failed, exiting");
    finish();
    return;
  }

  LOG_DBG("OTA", "WiFi connected, checking for update");

  {
    RenderLock lock(*this);
    state = CHECKING_FOR_UPDATE;
  }
  requestUpdateAndWait();

  // Install Other Firmware also looks at prereleases: it exists for a reader
  // that must get off this firmware, and refusing a build for its channel is
  // the same trap as refusing it for its version.
  const auto res = updater.checkForUpdate(allowAnyVersion);
  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update check failed: %d", res);
    {
      RenderLock lock(*this);
      state = FAILED;
    }
    return;
  }

  // In install-other-firmware mode the version comparison is not asked: the
  // server is expected to be offering something else entirely, usually stock or
  // another fork, and refusing it is what traps the user here.
  newVersionLine = std::string(tr(STR_NEW_VERSION)) + updater.getLatestVersion();
  currentVersionLine = std::string(tr(STR_CURRENT_VERSION)) + CROSSPOINT_VERSION;

  if (!allowAnyVersion && !updater.isUpdateNewer()) {
    LOG_DBG("OTA", "No new update available");
    {
      RenderLock lock(*this);
      state = NO_UPDATE;
    }
    return;
  }

  {
    RenderLock lock(*this);
    state = WAITING_CONFIRMATION;
  }
}

void OtaUpdateActivity::onEnter() {
  UiStatusActivity::onEnter();

  // Turn on WiFi immediately
  LOG_DBG("OTA", "Turning on WiFi...");
  WiFi.mode(WIFI_STA);

  // Launch WiFi selection subactivity
  LOG_DBG("OTA", "Launching WifiSelectionActivity...");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void OtaUpdateActivity::onExit() {
  Activity::onExit();

  // Success path reboots via the SHUTTING_DOWN state's plain ESP.restart()
  // (loop() above) so the new firmware boots normally. Back-out paths land
  // here with wifi still active; silent-restart to free the LWIP/mbedTLS
  // fragmentation, same as the other wifi activities.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

UiStatusActivity::StatusView OtaUpdateActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_UPDATE);
  switch (state) {
    case CHECKING_FOR_UPDATE:
      view.lines = {tr(STR_CHECKING_UPDATE), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case WAITING_CONFIRMATION:
      view.lines = {allowAnyVersion ? tr(STR_INSTALL_OTHER_FIRMWARE_PROMPT) : tr(STR_NEW_UPDATE),
                    currentVersionLine.c_str(), newVersionLine.c_str(), nullptr};
      // The two answers, on screen as well as on the keys: this screen used to
      // draw a pair of labels that no touch was ever routed to.
      view.cancelLabel = tr(STR_CANCEL);
      view.acceptLabel = tr(STR_UPDATE);
      view.backHint = tr(STR_CANCEL);
      view.confirmHint = tr(STR_UPDATE);
      break;
    case UPDATE_IN_PROGRESS:
      view.lines = {tr(STR_UPDATING), nullptr, nullptr, nullptr};
      view.showProgress = true;
      view.progressValue = static_cast<int>(updater.getProcessedSize());
      view.progressMax = static_cast<int>(updater.getTotalSize());
      view.progressLabel = bytesLine.c_str();
      view.backHint = "";
      break;
    case NO_UPDATE:
      // "Install anyway" is not a curiosity: on a device whose USB flashing the
      // vendor locked, an update server offering this same version is the only
      // route left to put another firmware on it.
      view.lines = {tr(STR_NO_UPDATE), newVersionLine.c_str(), nullptr, nullptr};
      view.cancelLabel = tr(STR_BACK);
      view.acceptLabel = tr(STR_INSTALL_ANYWAY);
      view.confirmHint = tr(STR_INSTALL_ANYWAY);
      break;
    case FAILED:
      view.lines = {tr(STR_UPDATE_FAILED), failedDetail, failedExtra.empty() ? nullptr : failedExtra.c_str(),
                    failedHint.empty() ? nullptr : failedHint.c_str()};
      break;
    case FINISHED:
      view.lines = {
          tr(STR_UPDATE_COMPLETE),
          "Flashed ok; if it boots back into Lector,",
          "update Lector first to 0.29.5 then",
          "reflash the other firmware.",
      };
      view.backHint = "";
      break;
    case WIFI_SELECTION:
    case SHUTTING_DOWN:
      // The WiFi picker owns the screen, and a restarting device has nothing to
      // say.
      view.hidden = true;
      break;
  }
  return view;
}

bool OtaUpdateActivity::handleCustomInput() {
  if (state == SHUTTING_DOWN) {
    ESP.restart();
    return true;
  }
  // Every other state answers on the two buttons alone.
  return state == CHECKING_FOR_UPDATE || state == UPDATE_IN_PROGRESS || state == WIFI_SELECTION;
}

void OtaUpdateActivity::onConfirmButton() {
  if (state == WAITING_CONFIRMATION) {
    runUpdateInstall();
    return;
  }
  if (state == NO_UPDATE) {
    allowAnyVersion = true;
    {
      RenderLock lock(*this);
      state = WAITING_CONFIRMATION;
    }
    requestUpdate();
  }
}

void OtaUpdateActivity::onBackButton() {
  if (state == WAITING_CONFIRMATION || state == FAILED || state == NO_UPDATE) finish();
}

const char* OtaUpdateActivity::detailFor(const OtaUpdater::OtaUpdaterError error) {
  switch (error) {
    case OtaUpdater::WRONG_DEVICE_ERROR:
      return tr(STR_FIRMWARE_WRONG_DEVICE);
    case OtaUpdater::INVALID_IMAGE_ERROR:
      return tr(STR_INVALID_FIRMWARE);
    case OtaUpdater::OOM_ERROR:
      return tr(STR_UPDATE_LOW_MEMORY);
    case OtaUpdater::HTTP_ERROR:
      return "Download failed";
    case OtaUpdater::INTERNAL_UPDATE_ERROR:
      return tr(STR_FIRMWARE_WRITE_FAILED);
    default:
      return nullptr;
  }
}

void OtaUpdateActivity::runUpdateInstall() {
  LOG_DBG("OTA", "New update available, starting download...");
  {
    RenderLock lock(*this);
    state = UPDATE_IN_PROGRESS;
  }
  requestUpdateAndWait();
  // A TLS record buffer needs room the reader does not have with WiFi up, so the
  // framebuffer's 48 KB are lent to wolfSSL for the transfer (see
  // TlsScratchHeap.h). Nothing may draw while they are lent, so the progress bar
  // holds at 0% for a secure download; the OTA Unlocker serves plain HTTP, where
  // the bar animates as before.
  const bool secure = updater.isDownloadSecure();
  drawingSuspended = secure;
  OtaUpdater::OtaUpdaterError res;
  {
    std::unique_ptr<GfxRenderer::FrameBufferLoan> loan;
    std::unique_ptr<tls_scratch::Session> tlsScratch;
    if (secure) {
      loan = std::make_unique<GfxRenderer::FrameBufferLoan>(renderer);
      tlsScratch = std::make_unique<tls_scratch::Session>();
      if (!tlsScratch->active()) {
        LOG_ERR("OTA", "Framebuffer not lent; the transfer runs on the heap alone");
      }
    }
    res = updater.installUpdate(
        [](void* ctx) {
          // immediate=true notifies the render task directly. The default deferred path only
          // sets a flag consumed at the end of ActivityManager::loop(), which never runs while
          // installUpdate() blocks this task.
          auto* self = static_cast<OtaUpdateActivity*>(ctx);
          if (self->drawingSuspended) return;  // the framebuffer is lent to wolfSSL
          // The throttle lives here, not in the paint: at two percent a step a
          // 4 MB image costs 50 e-ink passes instead of one per chunk. render()
          // used to return early to do this, which meant a repaint asked for by
          // anything else was dropped too.
          const int total = self->updater.getTotalSize();
          if (total <= 0) return;
          const unsigned int percent = static_cast<unsigned int>((self->updater.getProcessedSize() * 100) / total);
          if (self->lastUpdaterPercentage != UNINITIALIZED_PERCENTAGE && percent < self->lastUpdaterPercentage + 2) {
            return;
          }
          self->lastUpdaterPercentage = percent;
          self->bytesLine = std::to_string(self->updater.getProcessedSize()) + " / " + std::to_string(total);
          self->requestUpdate(true);
        },
        this, allowAnyVersion);
  }
  drawingSuspended = false;

  if (res != OtaUpdater::OK) {
    LOG_DBG("OTA", "Update failed: %d", res);
    {
      RenderLock lock(*this);
      failedDetail = detailFor(res);
      failedExtra.clear();
      failedHint.clear();
      if (res == OtaUpdater::WRONG_DEVICE_ERROR) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Image: %s, device: %s", firmware_flash::chipName(updater.getLastImageChip()),
                      firmware_flash::chipName(firmware_flash::runningPartitionChipId()));
        failedExtra = buf;
        failedHint = "Use firmware for this device's chip";
      } else if (res == OtaUpdater::INVALID_IMAGE_ERROR) {
        failedExtra = "Corrupt download: checksum mismatch";
      }
      state = FAILED;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    state = FINISHED;
  }
  requestUpdateAndWait();
  // Hold the completion screen briefly so the user sees it, then restart.
  delay(4000);
  {
    RenderLock lock(*this);
    state = SHUTTING_DOWN;
  }
}
