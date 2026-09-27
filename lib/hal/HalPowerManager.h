#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <InputManager.h>
#include <Logging.h>
#include <esp_pm.h>
#include <freertos/semphr.h>

#include <atomic>
#include <cassert>

#include "HalGPIO.h"

class HalPowerManager;
extern HalPowerManager powerManager;  // Singleton

class HalPowerManager {
  int normalFreq = 0;  // MHz, the frequency the build boots at; becomes esp_pm's max_freq_mhz

  mutable int _batteryCachedPercent = 0;         // Last read battery percentage (0-100)
  mutable unsigned long _batteryLastPollMs = 0;  // Timestamp of last battery read in milliseconds

  // The four PM locks this firmware owns. All are ESP_PM_CPU_FREQ_MAX, which in
  // the IDF power manager is the strongest mode: it pins the CPU at max_freq_mhz,
  // pins APB with it, and keeps the FreeRTOS idle task out of light sleep for as
  // long as any of them is held. One lock type therefore covers all three things
  // the old hand-rolled path did separately (raise the clock, hold APB steady,
  // refuse to sleep).
  //
  // Unlike the old single LockMode, esp_pm locks are reference counted, so nested
  // acquisitions are fine and the "Lock already held, ignore" case is gone.
  esp_pm_lock_handle_t perfLock = nullptr;      // a render is on the panel
  esp_pm_lock_handle_t wifiLock = nullptr;      // the modem is up
  esp_pm_lock_handle_t activityLock = nullptr;  // within IDLE_ACTIVITY_HOLD_MS of the last input
  bool wifiLockHeld = false;
  bool activityLockHeld = false;

  // USB is deliberately NOT a lock. A CPU_FREQ_MAX lock would also pin the clock
  // at 160 MHz for as long as a cable is plugged in, and the only thing USB
  // actually needs is for light sleep not to happen -- the USB Serial/JTAG PHY is
  // clocked from the PLL, which min 80 keeps running at every DFS point. So the
  // firmware registers an esp_pm skip-light-sleep callback instead, which vetoes
  // the sleep and leaves the frequency alone. See the .cpp for why the veto has
  // to be a callback rather than a lock taken in the main loop.
  void setUsbConsoleUp(bool up) const;

  // Depth of perfLock, for isPerfLockHeld(). esp_pm exposes no "is this held"
  // query and the answer decides which of two idle waits the main loop uses, so
  // the count is kept alongside. Bookkeeping only — the lock itself is counted
  // by the IDF.
  std::atomic<uint32_t> perfLockDepth{0};

  void setLock(esp_pm_lock_handle_t lock, bool& held, bool wanted);
  // Bumped by the main loop, read by the render task (see onEinkBusyWaitSlice).
  // Single writer, naturally aligned: the 32-bit access is atomic on RISC-V.
  volatile uint32_t mainLoopIterations = 0;

  // Cap on how long one slice may stay awake waiting for that iteration.
  // Productive yields measure ~0.4 ms, so two ticks is ample headroom.
  static constexpr unsigned SLICE_YIELD_MAX_TICKS = 2;

  // Set while the main loop is blocked in requestUpdateAndWait(). Captured in
  // begin(), which Arduino runs on the loop task, so the handle compare below
  // ignores any other task that might wait on a render.
  TaskHandle_t mainLoopTask = nullptr;
  volatile bool mainLoopBlocked = false;

  // Serializes wake-source arm -> esp_light_sleep_start() -> disarm for the render
  // task's BUSY-wait slice, which is now the only place this firmware calls
  // esp_light_sleep_start() by hand. Kept rather than removed with the main-loop
  // sleeper: direct displayBuffer() calls from setup() and loop() reach the slice
  // hook on the loop task, so two different tasks can still be inside it.
  SemaphoreHandle_t sleepMutex = nullptr;

 public:
  // The floor DFS is allowed to drop the CPU to, on every target. 80 and not the
  // 10 MHz the deleted manual downclock used on the C3, because 80 is the lowest
  // frequency that still comes off the PLL: below it the CPU switches to the
  // crystal and APB follows the CPU down
  // (esp_hw_support/port/esp32c3/rtc_clk.c:347), while every PLL frequency pins
  // APB at 80 MHz regardless of the CPU divider (same file, line 191). Holding
  // APB still is what makes the display and SD SPI safe under DFS without
  // auditing every transaction -- see the APB table at the top of the .cpp.
  //
  // It also keeps the PLL running, which the USB Serial/JTAG PHY needs, and
  // matches what the IDF would pick anyway: with WiFi compiled in,
  // esp_pm_configure() raises APB_MAX to MODEM_REQUIRED_MIN_APB_CLK_FREQ (80 MHz)
  // on this target, so a lower min only ever applies to PM_MODE_APB_MIN.
  //
  // The saving given up against 10 MHz is the idle-awake current between polls.
  // That window is small by construction, and the 10 MHz floor was already
  // measured not to pay for itself: the loop's old idle branch raced to sleep at
  // full clock on purpose, because this board's sleep-floor current is charged
  // per millisecond whatever the CPU is doing, so finishing a wake window ~16x
  // faster beat stretching it at 10 MHz (measured there: 8.8 mA for 4.5 ms per
  // wake). That branch is deleted by this change; the reasoning is why the floor
  // moving up costs less than it looks. Under tickless idle the chip now sleeps
  // through most of the window rather than running at any frequency.
  static constexpr int LOW_POWER_FREQ = 80;  // MHz

  // THE calibration knob for this whole change, and the one thing that cannot be
  // checked without the device in hand. For this long after the last input the
  // activity lock is held, which keeps the CPU at full speed and keeps the chip
  // out of light sleep entirely — the old IDLE_DOWNCLOCK_MS window, same 500 ms
  // default, now covering sleep as well as clock.
  //
  // Raise it if input feels laggy or a tap is ever dropped just after another
  // one; lower it (0 disables the hold) to let the chip sleep in the gaps
  // between rapid page turns too. Power cost and input snappiness trade directly
  // against each other here and nowhere else.
  static constexpr unsigned long IDLE_ACTIVITY_HOLD_MS = 500;

  // How long the main loop blocks per idle iteration. Under tickless idle a
  // blocked loop task is exactly how long the chip gets to sleep, so this is the
  // sleep granularity as well as the input sampling rate.
  //
  // One cadence, not the old code's two. The deleted manual sleeper used 50 ms
  // slices out here and had to arm a power-button GPIO level wake alongside the
  // timer to stay correct: committing a press needs two update() samples at least
  // 5 ms apart, and at 50 ms a short tap lands in one sample or none and is
  // dropped (that was a field report, not a theory). Tickless idle arms its own
  // timer wake and knows nothing about the power button, so at 50 ms the same bug
  // would come straight back. 10 ms samples any human tap at least twice and
  // needs no wake source the power manager does not already set up.
  //
  // The cost is wake overhead: roughly 100 sleep entries a second instead of 20.
  // It is bounded — this window only lasts from putting the book down until the
  // auto-sleep timeout takes the chip into deep sleep, which is minutes, not the
  // days that actually decide battery life. Raising this is the first thing to
  // try if the sleep ratio in the /perf summary looks poor, but only once
  // short-press behaviour has been checked on hardware at the new value.
  static constexpr unsigned long IDLE_POLL_MS = 10;

  // Panel rails off. Deliberately later than the idle poll backs off: the rails only
  // need to be down while the device sits, and powering them back up costs a PON wait on
  // the very next paint. Two seconds is long enough that ordinary reading — a page turn
  // every few seconds — never pays it, and short enough that a device put down never sits
  // powered long enough to drift. See the call site in loop() for the failure it fixes.
  static constexpr unsigned long IDLE_PANEL_POWER_OFF_MS = 2000;

  static constexpr unsigned long BATTERY_POLL_MS = 1500;  // ms

  // Timer bound for one BUSY-wait light-sleep slice. The refresh end itself
  // wakes exactly via GPIO level wake on the BUSY pin; the timer only bounds
  // how coarse main-loop button sampling gets during a long refresh.
  static constexpr unsigned long BUSY_SLEEP_SLICE_MS = 20;  // ms

  void begin();

  // Setup wake up GPIO and enter deep sleep
  void startDeepSleep() const;

  // Call once per main-loop iteration with the time since the last accepted input.
  // Publishes the USB console state and takes or releases the WiFi and
  // recent-activity locks. Each is something the IDF power manager cannot see for
  // itself:
  //   - Arduino's HWCDC drives the USB Serial/JTAG peripheral directly and, unlike
  //     the IDF's own driver, registers nothing with esp_pm. Left alone, the idle
  //     task light-sleeps the chip and the sleep path disables the USJ pad
  //     (esp_hw_support/sleep_console.c), dropping an enumerated CDC link
  //     mid-session. Published as a flag here, enforced by the skip-light-sleep
  //     callback -- see setUsbConsoleUp() above.
  //   - the WiFi driver does take its own locks, but only ESP_PM_APB_FREQ_MAX: a
  //     download would still run with the CPU at LOW_POWER_FREQ. The old
  //     setPowerSaving() forced full speed whenever the modem was up; this keeps
  //     that contract.
  //   - IDLE_ACTIVITY_HOLD_MS after the last input, see that constant.
  // Idempotent: acquiring an already-held lock or releasing an unheld one is a
  // no-op, so this is safe to call at 100 Hz.
  void updateLocks(const HalGPIO& gpio, unsigned long idleMs);

  // True while a render holds the performance lock. The main loop uses it to pick
  // between its two idle waits — see the comment at idlePoll() in main.cpp.
  bool isPerfLockHeld() const { return perfLockDepth.load(std::memory_order_relaxed) != 0; }

  // One line of measured power-manager state for the /perf session summary:
  // current CPU frequency, accumulated light-sleep time against uptime, the sleep
  // count, and which of this firmware's locks are held right now. Formatting and
  // arithmetic live in lib/PowerReport (host-tested); this only supplies the
  // numbers. Returns the number of characters written, 0 if PM is not compiled in.
  size_t formatPowerReport(char* out, size_t outLen) const;

  // BUSY-wait slice hook (EpdBus::setBusyWaitSliceHook): light-sleep the chip
  // for up to BUSY_SLEEP_SLICE_MS while the panel refreshes autonomously,
  // waking early the moment the BUSY pin leaves `busyLevel`. Returns false
  // WITHOUT sleeping when unsafe (WiFi active, USB connected, or sleep
  // rejected) — the SDK then falls back to its plain poll delay. Called from
  // the render task; safe by construction (the waiter IS the locked task).
  // Leaves the CPU clock alone: the chip is asleep for most of the wait, so a
  // downclock buys nothing and only slows the main loop's awake windows.
  bool onEinkBusyWaitSlice(int8_t busyPin, uint8_t busyLevel);

  // Call at the end of every main-loop iteration; paces the slice hook's yield.
  // Read-then-assign, not ++: C++20 deprecates increment on a volatile lvalue.
  void noteMainLoopIteration() { mainLoopIterations = mainLoopIterations + 1; }

  // Bracket a blocking wait on the render task (requestUpdateAndWait). While the
  // main loop is parked there it cannot poll, so the slice hook skips its yield
  // instead of burning the full cap awake once per slice for nothing.
  void noteRenderWaitBegin() {
    if (xTaskGetCurrentTaskHandle() == mainLoopTask) mainLoopBlocked = true;
  }
  void noteRenderWaitEnd() {
    if (xTaskGetCurrentTaskHandle() == mainLoopTask) mainLoopBlocked = false;
  }

  // Get battery percentage (range 0-100)
  uint16_t getBatteryPercentage() const;
  // Battery voltage in millivolts, uncached; 0 when the board cannot read one.
  // For the diagnostics file: a percentage rounds to tens, a voltage says
  // whether a 4 MB flash write was running on a cell about to brown out.
  uint16_t getBatteryMillivolts() const;

  // RAII helper: full performance for the scope it lives in. Call sites are
  // unchanged from when this drove setCpuFrequencyMhz() by hand; the body now
  // takes an ESP_PM_CPU_FREQ_MAX lock instead, which additionally guarantees a
  // steady APB clock and no light sleep for the duration. That combination is
  // what makes the display SPI safe under DFS — see the APB audit in the .cpp.
  //
  // Nesting is allowed now (esp_pm locks are reference counted), unlike the old
  // single-slot LockMode which logged an error and gave up on the second holder.
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
