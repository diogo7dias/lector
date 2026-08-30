#include "SdFirmwareUpdateActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <esp_ota_ops.h>

#include "MappedInputManager.h"
#include "activities/home/FileBrowserActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/FirmwareFlasher.h"

void SdFirmwareUpdateActivity::onEnter() {
  UiStatusActivity::onEnter();
  // Build-identity marker — confirms which firmware build owns the SD update flow.
  LOG_INF("FW", "SdFirmwareUpdateActivity build=%s %s recovery=%d", __DATE__, __TIME__, recoveryMode ? 1 : 0);
  state = State::PICKING;
  launchPicker();
}

void SdFirmwareUpdateActivity::launchPicker() {
  // Reuse the standard file browser, restricted to .bin files only.
  startActivityForResult(
      std::make_unique<FileBrowserActivity>(renderer, mappedInput, "/", FileBrowserActivity::Mode::PickFirmware),
      [this](const ActivityResult& result) { onPickerResult(result); });
}

void SdFirmwareUpdateActivity::onPickerResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Recovery mode: re-launch the picker so the user cannot escape into a half-initialised UI.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  const auto* path = std::get_if<FilePathResult>(&result.data);
  if (!path) {
    LOG_ERR("FW", "Picker returned no path");
    finish();
    return;
  }
  firmwarePath = path->path;
  LOG_DBG("FW", "Selected: %s", firmwarePath.c_str());

  {
    RenderLock lock(*this);
    state = State::VALIDATING;
  }
  requestUpdateAndWait();

  if (!validateFirmware()) {
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  promptConfirmation();
}

bool SdFirmwareUpdateActivity::validateFirmware() {
  HalFile file;
  if (!Storage.openFileForRead("FW", firmwarePath.c_str(), file) || !file) {
    errorMessage = tr(STR_FIRMWARE_FILE_OPEN_FAILED);
    return false;
  }
  firmwareSize = file.fileSize();
  file.close();

  // Resolve the next-update partition directly via the OTA API. Previously this
  // probed via Update.begin(firmwareSize)/Update.abort() to learn the partition
  // size, which had the side effect of erasing partition state and was wasted
  // work since we only need the size bound for validation here.
  const esp_partition_t* dest = esp_ota_get_next_update_partition(nullptr);
  if (!dest) {
    LOG_ERR("FW", "no next-update partition available");
    errorMessage = tr(STR_INVALID_FIRMWARE);
    return false;
  }
  const size_t partitionLimit = dest->size;
  if (firmwareSize > partitionLimit) {
    LOG_ERR("FW", "firmware (%u bytes) exceeds partition (%u bytes)", static_cast<unsigned>(firmwareSize),
            static_cast<unsigned>(partitionLimit));
    errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    return false;
  }

  // Run the same end-to-end integrity check (header / segment table / XOR checksum / SHA256
  // trailer) that the shared firmware-flasher applies right before raw-writing otadata. This
  // catches truncated or corrupted .bin files at confirmation time, before the user ever sees
  // the "Updating…" progress bar.
  const auto vr = firmware_flash::validateImageFile(firmwarePath.c_str(), partitionLimit);
  if (vr != firmware_flash::Result::OK) {
    LOG_ERR("FW", "image validation failed: %s", firmware_flash::resultName(vr));
    if (vr == firmware_flash::Result::TOO_LARGE) {
      errorMessage = tr(STR_FIRMWARE_TOO_LARGE);
    } else if (vr == firmware_flash::Result::TOO_SMALL) {
      errorMessage = tr(STR_FIRMWARE_TOO_SMALL);
    } else if (vr == firmware_flash::Result::BAD_CHIP) {
      errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else {
      errorMessage = tr(STR_INVALID_FIRMWARE);
    }
    return false;
  }
  return true;
}

void SdFirmwareUpdateActivity::promptConfirmation() {
  {
    RenderLock lock(*this);
    state = State::CONFIRMING;
  }
  // Show "Update firmware?" with the file path as the body line.
  std::string heading = tr(STR_FIRMWARE_UPDATE_PROMPT);
  // Use the basename only to keep the body short.
  std::string body = firmwarePath;
  const auto pos = body.find_last_of('/');
  if (pos != std::string::npos) body = body.substr(pos + 1);

  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading, body),
                         [this](const ActivityResult& result) { onConfirmationResult(result); });
}

void SdFirmwareUpdateActivity::onConfirmationResult(const ActivityResult& result) {
  if (result.isCancelled) {
    if (recoveryMode) {
      // Go back to the picker rather than exiting recovery.
      launchPicker();
      return;
    }
    finish();
    return;
  }

  {
    RenderLock lock(*this);
    state = State::UPDATING;
    writtenBytes = 0;
    lastRenderedPercent = 101;
  }
  requestUpdateAndWait();
  performUpdate();
}

void SdFirmwareUpdateActivity::performUpdate() {
  LOG_INF("FW", "SD update: %s (%u bytes)", firmwarePath.c_str(), static_cast<unsigned>(firmwareSize));

  auto progressCb = +[](size_t written, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    self->writtenBytes = written;
    self->firmwareSize = total;
    // Once per percent, and here rather than in the paint: a render that
    // returned early to throttle itself also dropped repaints asked for by
    // anything else. immediate=true wakes the render task directly, because
    // this is a tight sync loop and the main loop will not drain the flag.
    if (!self->percentAdvanced()) return;
    self->requestUpdate(true);
  };

  // The readback pass streams the image a second time, so it gets its own
  // labelled progress bar rather than leaving the write bar parked at 100%.
  auto verifyCb = +[](size_t checked, size_t total, void* ctx) {
    auto* self = static_cast<SdFirmwareUpdateActivity*>(ctx);
    if (self->state != State::VERIFYING) {
      self->state = State::VERIFYING;
      self->lastRenderedPercent = 101;
    }
    self->writtenBytes = checked;
    self->firmwareSize = total;
    if (!self->percentAdvanced()) return;
    self->requestUpdate(true);
  };

  // Re-validate at flash time (TOCTOU): SD is removable, so don't trust the
  // pre-confirmation pass. The alreadyValidated parameter on the API stays
  // for callers (e.g. an OTA staging path) where the same byte stream was
  // just hashed and there's no removable-media gap.
  const auto result = firmware_flash::flashFromSdPath(firmwarePath.c_str(), progressCb, this,
                                                      /*alreadyValidated=*/false, verifyCb);
  if (result != firmware_flash::Result::OK) {
    LOG_ERR("FW", "flash failed: %s", firmware_flash::resultName(result));
    // BAD_CHIP here is the TOCTOU re-validation catching a wrong-MCU image the
    // pre-confirmation pass missed (e.g. the SD card was swapped).
    if (result == firmware_flash::Result::BAD_CHIP) {
      errorMessage = tr(STR_FIRMWARE_WRONG_DEVICE);
    } else if (result == firmware_flash::Result::VERIFY_FAIL) {
      // The bytes in flash do not match the file. otadata was left alone, so
      // the device still boots the firmware it is running now.
      errorMessage = tr(STR_FIRMWARE_VERIFY_FAILED);
    } else {
      // The code goes on screen: on a USB-locked reader a photo of this screen
      // is often the only report we get, and "write failed" alone covers
      // everything from a missing OTA slot to a bad erase.
      errorMessage = std::string(tr(STR_FIRMWARE_WRITE_FAILED)) + " (" + firmware_flash::resultName(result) + ")";
    }
    RenderLock lock(*this);
    state = State::FAILED;
    requestUpdate();
    return;
  }

  LOG_INF("FW", "SD firmware update complete, restarting");
  {
    RenderLock lock(*this);
    state = State::SUCCESS;
  }
  requestUpdateAndWait();
  delay(1500);
  ESP.restart();
}

bool SdFirmwareUpdateActivity::percentAdvanced() {
  const unsigned int percent = firmwareSize > 0 ? static_cast<unsigned int>((writtenBytes * 100) / firmwareSize) : 0;
  if (percent == lastRenderedPercent) return false;
  lastRenderedPercent = percent;
  return true;
}

UiStatusActivity::StatusView SdFirmwareUpdateActivity::statusView() const {
  StatusView view;
  view.title = recoveryMode ? tr(STR_RECOVERY_MODE) : tr(STR_SD_FIRMWARE_UPDATE);
  switch (state) {
    case State::VALIDATING:
      view.lines = {tr(STR_VALIDATING_FIRMWARE), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case State::UPDATING:
    case State::VERIFYING:
      // The readback pass says so rather than leaving the write bar parked at
      // 100% while it runs.
      view.lines = {state == State::VERIFYING ? tr(STR_FIRMWARE_CHECKING) : tr(STR_UPDATING),
                    tr(STR_FIRMWARE_UPDATE_DO_NOT_POWER_OFF), nullptr, nullptr};
      view.showProgress = true;
      view.progressValue = static_cast<int>(writtenBytes);
      view.progressMax = firmwareSize > 0 ? static_cast<int>(firmwareSize) : 1;
      view.backHint = "";
      break;
    case State::SUCCESS:
      view.lines = {tr(STR_UPDATE_COMPLETE), tr(STR_RESTARTING_HINT), nullptr, nullptr};
      view.backHint = "";
      break;
    case State::FAILED:
      view.lines = {tr(STR_UPDATE_FAILED), errorMessage.empty() ? nullptr : errorMessage.c_str(), nullptr, nullptr};
      break;
    case State::PICKING:
    case State::CONFIRMING:
      // The picker and the confirmation are their own activities. In recovery
      // mode there is a hint behind them, because that is the whole screen a
      // reader booting into recovery sees first.
      if (recoveryMode) {
        view.lines = {tr(STR_RECOVERY_MODE_HINT), nullptr, nullptr, nullptr};
        view.backHint = "";
      } else {
        view.hidden = true;
      }
      break;
  }
  return view;
}

// Either button leaves a failure: in recovery mode back to the picker, because
// a reader with no other way in has to be able to try a different image.
void SdFirmwareUpdateActivity::onBackButton() {
  if (state != State::FAILED) return;
  if (recoveryMode) {
    state = State::PICKING;
    launchPicker();
    return;
  }
  finish();
}

void SdFirmwareUpdateActivity::onConfirmButton() { onBackButton(); }
