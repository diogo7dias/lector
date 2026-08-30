#pragma once

#include <string>

#include "activities/UiStatusActivity.h"

/**
 * SD-card based firmware update activity.
 *
 * Flow:
 *  1) onEnter -> push FileBrowserActivity in PickFirmware mode (only .bin files visible).
 *  2) On result: validate the .bin (header magic, size fits OTA partition).
 *  3) Push ConfirmationActivity ("Update firmware?").
 *  4) On confirm: stream the file into the OTA partition, drawing a progress bar, then
 *     read every byte back and compare it against the file; on success ESP.restart().
 *
 * Used both from Settings -> System -> "SD Card Firmware Update", and as the only
 * activity launched in boot recovery mode (left side button + power on X3).
 */
class SdFirmwareUpdateActivity : public UiStatusActivity {
 public:
  enum class State {
    PICKING,
    VALIDATING,
    CONFIRMING,
    UPDATING,
    VERIFYING,
    SUCCESS,
    FAILED,
  };

  explicit SdFirmwareUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool recoveryMode = false)
      : UiStatusActivity("SdFirmwareUpdate", renderer, mappedInput), recoveryMode(recoveryMode) {}

  void onEnter() override;
  bool preventAutoSleep() override {
    return state == State::UPDATING || state == State::VERIFYING || state == State::VALIDATING;
  }
  bool skipLoopDelay() override { return state == State::UPDATING || state == State::VERIFYING; }

 protected:
  StatusView statusView() const override;
  void onBackButton() override;
  void onConfirmButton() override;

 private:
  State state = State::PICKING;
  bool recoveryMode = false;

  std::string firmwarePath;
  size_t firmwareSize = 0;
  size_t writtenBytes = 0;
  // Out of range on purpose: the first progress callback must always be let
  // through, and every real percent is 0 to 100.
  unsigned int lastRenderedPercent = 101;
  std::string errorMessage;

  // True when the write has moved a whole percent since the last repaint.
  bool percentAdvanced();
  void launchPicker();
  void onPickerResult(const ActivityResult& result);
  bool validateFirmware();
  void promptConfirmation();
  void onConfirmationResult(const ActivityResult& result);
  void performUpdate();
};
