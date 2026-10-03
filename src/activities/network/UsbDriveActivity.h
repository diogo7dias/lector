#pragma once

#include "activities/UiStatusActivity.h"

// File Transfer > USB Drive (X4 Pro only): the SD card shows up on a computer as a
// removable disk. HalStorage refuses every firmware file call while the computer
// holds the card, and the only way out is a reboot to Home.
class UsbDriveActivity final : public UiStatusActivity {
 public:
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("UsbDrive", renderer, mappedInput) {}

  void onEnter() override;
  bool requiresExclusiveStorageLoop() const override { return true; }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;

 private:
  enum class Phase : uint8_t { Preparing, StartFailed, Waiting, Connected, IoError };

  static constexpr unsigned long HOST_WAIT_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  static constexpr unsigned long START_FAILURE_TIMEOUT_MS = 30UL * 1000UL;
  static constexpr unsigned long FORCED_DISCONNECT_TIMEOUT_MS = 1000UL;

  bool canLeave() const { return phase == Phase::Waiting || phase == Phase::StartFailed; }
  void enterPhase(Phase next);
  void restartToHome();

  Phase phase = Phase::Preparing;
  bool disconnectRequested = false;
  unsigned long phaseStartedAt = 0;
};
