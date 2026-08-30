#pragma once

#include "activities/UiStatusActivity.h"

// Manual NTP resync action. Runs a forced sync (bypassing the once-per-device debounce),
// reports success/failure, then waits for Back. If WiFi is not connected yet, it reuses the
// normal WiFi selection flow first.
class ClockSyncActivity final : public UiStatusActivity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("ClockSync", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  bool skipLoopDelay() override { return true; }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;

 private:
  enum State { SYNCING, SUCCESS, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  // "Current time: 08:56 PM" for the success state, built once the sync lands.
  char syncedLine[80] = {0};
  bool shouldTearDownWifiOnExit = false;

  void runSync();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
};
