#pragma once

namespace busy_wait_power {

// Pure state machine for temporarily lowering the CPU clock during an e-ink
// BUSY wait while ActivityManager::renderTaskLoop holds HalPowerManager::Lock.
// Tracks whether the BUSY wait itself was what lowered the clock so:
//   - an idle device (e.g. display.powerOffPanel() at 2 s idle) stays at low
//     power after BUSY completes, and
//   - a mid-BUSY wakeup (input or SD access via holdNormalSpeedForStorage())
//     keeps the clock restored without a redundant restore on BUSY exit.
struct State {
  bool isLowPower = false;
  bool loweredByBusyWait = false;

  bool shouldLowerOnBusyBegin(const bool wifiActive) const { return !wifiActive && !isLowPower; }

  void onBusyLowered() {
    isLowPower = true;
    loweredByBusyWait = true;
  }

  // Returns true if endBusyWait() should restore normal CPU frequency.
  bool takeRestoreOnBusyEnd() {
    if (!loweredByBusyWait) return false;
    loweredByBusyWait = false;
    return isLowPower;
  }

  void onRestoredToNormal() {
    isLowPower = false;
    loweredByBusyWait = false;
  }
};

}  // namespace busy_wait_power
