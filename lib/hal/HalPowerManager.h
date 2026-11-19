#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <freertos/semphr.h>

#include <cassert>

#include "BatteryPercentPoll.h"
#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

// CrossPoint's power manager: drop the CPU clock after a stretch without input, no
// automatic light sleep. Deep sleep is the only sleep the chip takes.
class HalPowerManager {
  int normalFreq = 0;  // MHz
  bool isLowPower = false;
  unsigned long storageWokeAtMs = 0;  // last time a card access had to restore full speed

  mutable BatteryPercentPoll _batteryPoll;

  enum LockMode { None, NormalSpeed };
  LockMode currentLockMode = None;
  SemaphoreHandle_t modeMutex = nullptr;  // Protect access to currentLockMode
  bool chargeWake = false;                // see setChargeWake()

 public:
#if BOARD_HAS_PSRAM
  // The crystal, the lowest clock the S3 datasheet gives an idle current for: 13.2 mA
  // against 22.0 mA at 80 MHz (Table 5-9, peripheral clocks off). Below 80 MHz the CPU
  // leaves the PLL, which the flash, the PSRAM and the SD host are timed against: see
  // setCpuClock() and holdNormalSpeedForStorage() for what that takes.
  static constexpr int LOW_POWER_FREQ = 40;  // MHz
#else
  static constexpr int LOW_POWER_FREQ = 10;  // MHz
#endif
  // How long after the last input the clock drops. CrossPoint's 3 s on the C3 boards.
  // X4 Pro: 1 s. Every wait for the next page turn already runs at the low clock, so this
  // only removes two seconds of full clock per turn. Work that outlives a render without
  // input (the reader's idle prewarm and progress save) takes a Lock instead of relying
  // on the window.
#if BOARD_HAS_PSRAM
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 1000;  // ms
#else
  static constexpr unsigned long IDLE_POWER_SAVING_MS = 3000;  // ms
#endif

  // Panel rails off. The rails only need to be down while the device sits, and powering
  // them back up costs a PON wait on the very next paint. Two seconds is long enough that
  // ordinary reading never pays it. See the call site in loop() for the failure it fixes.
  static constexpr unsigned long IDLE_PANEL_POWER_OFF_MS = 2000;

  void begin();

  // Control CPU frequency for power saving
  void setPowerSaving(bool enabled);

  // Called with the storage mutex held, before the card is touched. The S3's SD host
  // must not run a transfer on the crystal clock (ESP-IDF's own driver holds an APB lock
  // for the same reason), so a card access made while idle brings full speed back first.
  // Lowering the clock takes the same mutex, which makes the two exclusive.
  void holdNormalSpeedForStorage();

  // Setup wake up GPIO and enter deep sleep
  [[noreturn]] void startDeepSleep() const;

  // Wake on USB plug-in: deep sleep also wakes when the charger's status line starts charging, so the native
  // USB comes up without the power button. X4 Pro only (supportsChargeWake()); the setting is applied before
  // each sleep. Not armed when the sleep begins while already charging.
  void setChargeWake(bool enabled) { chargeWake = enabled; }
  static bool supportsChargeWake();
  static bool isCharging();  // charger STAT line high; only meaningful where supportsChargeWake()

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
