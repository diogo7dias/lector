#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

// CrossPoint's power manager: drop the CPU clock after a stretch without input, no
// automatic light sleep. Deep sleep is the only sleep the chip takes.
class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode

 public:
#if BOARD_HAS_PSRAM
  static constexpr int LOW_POWER_FREQ = 80;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms

  // Panel rails off. The rails only need to be down while the device sits, and powering
  // them back up costs a PON wait on the very next paint. Two seconds is long enough that
  // ordinary reading never pays it. See the call site in loop() for the failure it fixes.
  static constexpr unsigned long IDLE_PANEL_POWER_OFF_MS = 2000;

  static constexpr unsigned long BATTERY_POLL_MS = 1500;  // ms

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Setup wake up GPIO and enter deep sleep
  [[noreturn]] void startDeepSleep() const;

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;
  // Battery voltage in millivolts, uncached; 0 when the board cannot read one.
  // For the diagnostics file: a percentage rounds to tens, a voltage says
  // whether a 4 MB flash write was running on a cell about to brown out.
  uint16_t getBatteryMillivolts() const;

  // RAII helper class to manage power saving locks
  // Usage: create an instance of Lock in a scope to disable power saving, for example when running a task that needs
  // full performance. When the Lock instance is destroyed (goes out of scope), power saving will be re-enabled.
  class Lock {
    friend class HalPowerManager;
    bool valid = false;

   public:
    explicit Lock();
    ~Lock();

    // Non-copyable and non-movable
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;
  };
};
