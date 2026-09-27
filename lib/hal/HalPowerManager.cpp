// Power management on this firmware is the ESP-IDF power manager: dynamic
// frequency scaling plus FreeRTOS tickless idle, configured once in begin().
// This file used to run its own, and no longer does — two power managers
// disagreeing about the clock is worse than either of them alone.
//
// What was deleted, and why the IDF covers it:
//   setPowerSaving() drove Arduino's setCpuFrequencyMhz() after a 500 ms idle
//     threshold. That writes rtc_clk behind esp_pm's back, so with PM enabled
//     the two fight: esp_pm restores its own mode on the next lock change and
//     Arduino's cached APB state diverges from the hardware. DFS now does the
//     same job continuously and without a threshold.
//   lightSleep() light-slept the main loop in fixed 50 ms slices. Tickless idle
//     sleeps whenever every task is blocked, which includes the 0–1000 ms window
//     after an input that the 50 ms sleeper never reached.
//
// What was kept, and why the IDF cannot cover it:
//   onEinkBusyWaitSlice() still calls esp_light_sleep_start() by hand. It wakes
//     on the panel's BUSY pin leaving its active level, and tickless idle has no
//     way to know about that wake source. It cannot race the idle task: the
//     render task is running (so no core is idle), and ActivityManager holds a
//     HalPowerManager::Lock across the whole render, which keeps esp_pm out of
//     PM_MODE_LIGHT_SLEEP for the duration.
//
// ---------------------------------------------------------------------------
// APB audit -- what DFS does to each bus, checked against the IDF for these
// targets rather than assumed. It comes out green because of one decision, so
// that decision comes first.
//
//   min_freq_mhz is 80 on BOTH targets (HalPowerManager::LOW_POWER_FREQ), not
//   the 10 MHz the deleted manual downclock used on the C3.
//
//   Clock derivation (components/esp_hw_support/port/esp32{c3,s3}/rtc_clk.c):
//     a CPU frequency taken from the PLL pins APB at 80 MHz no matter what the
//       CPU divider is  -- rtc_clk_cpu_freq_to_pll_mhz(), C3 line 191.
//     a CPU frequency taken from the crystal sets APB to the CPU frequency
//       -- rtc_clk_cpu_freq_to_xtal(), C3 line 347.
//   80 MHz is the lowest PLL frequency on both parts. Pinning min there means
//   every DFS point this firmware can occupy is a PLL point, so APB is 80 MHz in
//   all of them and the bus dividers computed at boot stay correct forever.
//   That is the whole reason the table below has no unresolved row: at min 10 the
//   C3 would run APB at 10 MHz while Arduino's calculateApb() still returned a
//   hardcoded 80 MHz, and the divider for every APB-clocked peripheral would be
//   wrong for as long as the chip sat idle.
//
//   esp_pm_configure() independently arrives at the same 80: with WiFi compiled
//   in it raises APB_MAX to MODEM_REQUIRED_MIN_APB_CLK_FREQ, so the modes it
//   builds are CPU_MAX = the boot frequency, APB_MAX = 80, APB_MIN = 80, and
//   light sleep = APB_MIN (esp_pm/pm_impl.c, esp_pm_configure()).
//
//   Bus                          | APB at min 80        | Verdict
//   -----------------------------+----------------------+-------------------------
//   Display SPI (EpdBus)         | 80 MHz, never moves  | SAFE, and no longer
//     Arduino SPIClass -> esp32-hal-spi.c. SPI_CLK_SRC_DEFAULT is SOC_MOD_CLK_APB
//     on both targets, and Arduino's getApbFrequency() returns the compile-time
//     APB_CLK_FREQ (80 MHz) on everything except the original ESP32/S2. At min 80
//     that hardcoded answer is the truth, so the divider programmed at begin()
//     stays right at every DFS point and no lock is needed for correctness.
//     Renders take one anyway (ActivityManager holds HalPowerManager::Lock across
//     the whole render) because a render wants the CPU fast and wants no light
//     sleep, but the panel clock no longer depends on that.
//   SD card SPI (SdFat, same bus) | 80 MHz, never moves | SAFE, same mechanism.
//     This is the row that was unresolved at min 10 and is now settled the same
//     way as the panel: the card's divider is computed once from an APB that no
//     longer changes.
//   Touch I2C (GT911/FT5x06)     | irrelevant           | SAFE, clock-independent.
//     I2C_CLK_SRC_DEFAULT is SOC_MOD_CLK_XTAL on both the C3 and the S3
//     (components/soc/esp32{c3,s3}/include/soc/clk_tree_defs.h), so the SCL
//     timing registers come off the crystal and DFS cannot move them.
//   Serial log (Arduino HWCDC)   | irrelevant           | SAFE from DFS.
//     The USB Serial/JTAG peripheral is clocked from the USB PHY off the PLL, not
//     APB -- and min 80 keeps the PLL running at every DFS point, which a 10 MHz
//     crystal mode would not have. NOT safe from light sleep, which disables the
//     USJ pad (esp_hw_support/sleep_console.c; C3 and S3 both have
//     SOC_USB_SERIAL_JTAG_SUPPORT_LIGHT_SLEEP undefined, i.e. 0). That is what the
//     skip-light-sleep callback below is for.
//   esp_timer / millis()         | irrelevant           | SAFE.
//     esp_timer runs off SYSTIMER (crystal-derived), and the ROM microsecond delay
//     is rescaled by rtc_clk_cpu_freq_set_config_fast() on every switch.
//
//   One frequency point is still below 80: light-sleep ENTRY drops the CPU to the
//   crystal (rtc_clk_cpu_freq_set_xtal_for_sleep()), so APB is 40 MHz for the
//   instant between entry and the chip stopping, and again on the way out before
//   the mode is restored. Nothing can be mid-transaction there. The idle task only
//   sleeps when every task is blocked, a render holds a CPU_FREQ_MAX lock that
//   keeps the mode out of light sleep for its whole duration, and the one
//   hand-rolled sleeper left (onEinkBusyWaitSlice) only runs while the panel is
//   refreshing itself with the bus idle.
//
// Verification without a multimeter: turn on Performance Timings, read, lock the
// device, then read the "pm ..." line in /perf/<device>-NNN.csv on the card.

#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <PowerReport.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <soc/soc_caps.h>
#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
// esp_private/, because the IDF exposes no public way to veto an automatic light
// sleep. The alternative is a PM lock, which vetoes sleep only as a side effect of
// also pinning the CPU frequency -- see setUsbConsoleUp() below for why that is the
// wrong trade here. The header is stable across 5.x and the two slots it manages are
// accounted for at the registration site.
#include <esp_private/pm_impl.h>
#endif

#include <cassert>
#include <cstring>

#include "HalGPIO.h"

HalPowerManager powerManager;  // Singleton instance

// GPIO13 is the flash SPIWP pad (unused in DIO flash mode), rewired on the Xteink boards --
// but to DIFFERENT things per board, which this file long got wrong:
//   X4: the battery-latch MOSFET gate. High keeps the battery connected, low powers off.
//   X3: the SD card's power rail enable, active high. It is NOT the off switch there.
// Proven on hardware 2026-08-01 with probe build lector.c 0.7.0-wire13, which stopped driving
// the pin on an X3: the reader still powered off both ways, and the sleep wallpaper came back
// half drawn because that image is read from the SD card whose rail had just been dropped.
//
// The handling below is correct for both boards even so, which is why it stays shared: high
// while awake and across light sleep (X4 battery held / X3 card powered), low on the way into
// deep sleep (X4 powers off / X3 stops the card draining while the reader is off). That second
// half is the same saving upstream #2774 and #2808 set out to make, already in place here.
// Anything that touches this pin must satisfy BOTH meanings, or state which board it is for.
static constexpr gpio_num_t GPIO_BATTERY_LATCH = GPIO_NUM_13;

// Only a board that latches its battery through a *flash* pad needs the sleep-time hold
// below. The IDF flash-leakage workaround (CONFIG_ESP_SLEEP_FLASH_LEAKAGE_WORKAROUND) pulls
// DIO-unused flash pads low on light-sleep entry, which on those boards is a hard power-off.
// Boards that latch through an ordinary GPIO (BoardConfig power.latch0/latch1, asserted by
// holdPowerRails()) keep their output level across light sleep and need none of this.
//
// BOTH Xteink boards, for two different reasons that happen to want the same pin behaviour.
// GPIO13 is the battery latch on the X4 and the SD-rail power enable on the X3, and either
// way it must be pinned high across light sleep and driven low into deep sleep.
//
// Narrowing this to the X4 was tried twice on 2026-08-01 and failed on an X3 both times, in
// the same way: the sleep wallpaper came back half drawn or almost blank. That image is read
// from the SD card, and this hold is the only thing keeping the card's rail asserted through
// a light sleep. The SDK cannot do it -- SDCardManager asserts the rail on init and
// PowerManager::powerDownRailsForSleep() drops it for deep sleep, but neither knows GPIO13 is
// a flash pad, and the IDF flash-leakage workaround pulls DIO-unused flash pads low on every
// light-sleep entry. Only a pad hold survives that, and only this file knows to apply one.
//
// The isXteinkDevice() guard is also what keeps this off non-Xteink boards, where GPIO13 is an
// ordinary signal rather than a power control -- on the X4 Pro it is the display chip select
// (upstream #2998), so driving and holding it here would break the panel.
static bool hasFlashPadBatteryLatch() { return gpio.isXteinkDevice(); }

// Pin the battery latch high and pad-hold it across a light sleep. The hold overrides both
// the sleep-time pull and any pad re-muxing on the wake path, and is deliberately left
// enabled while running: the wake path may restore flash-pad muxing, so even a brief release
// between slices can drop the latch. startDeepSleep() is the only place that releases it.
// Level is set BEFORE direction so the pad never glitches low on the way to output mode.
static void holdBatteryLatchForSleep() {
  if (!hasFlashPadBatteryLatch()) {
    return;
  }
  gpio_set_level(GPIO_BATTERY_LATCH, 1);
  gpio_set_direction(GPIO_BATTERY_LATCH, GPIO_MODE_OUTPUT);
  gpio_hold_en(GPIO_BATTERY_LATCH);
}

namespace {

// Accumulated power-manager light-sleep time, and how many sleeps it took.
// Written from the light-sleep exit callback, which runs in IDLE task context on
// whichever core went to sleep last, and read by the loop task when the session
// summary is printed. The spinlock is for the 64-bit accumulator: a 32-bit core
// reads it in two halves, so an unguarded read taken across an update reports a
// number that was never true. One spinlock take per light sleep is nothing next
// to the sleep entry it rides along with.
portMUX_TYPE sleepStatsLock = portMUX_INITIALIZER_UNLOCKED;
uint64_t sleepAccumUs = 0;
uint32_t sleepAccumCount = 0;

// True while a USB cable is attached, republished by updateLocks() at the loop's
// poll rate. DRAM_ATTR because the only reader is IRAM_ATTR code: a flash-resident
// object would fault if the cache were ever down when the idle task asked.
DRAM_ATTR volatile bool usbConsoleUp = false;

#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
// esp_pm asks this on every attempt to enter automatic light sleep, from the
// FreeRTOS idle task, inside its own switch-lock critical section
// (esp_pm/pm_impl.c, should_skip_light_sleep()). One volatile load and nothing
// else: no logging, no allocation, no blocking call.
//
// Answering true is the whole USB story. The C3 and the S3 both leave
// SOC_USB_SERIAL_JTAG_SUPPORT_LIGHT_SLEEP undefined, so the IDF sleep path calls
// sleep_console_usj_pad_backup_and_disable() and takes the USB Serial/JTAG pad
// down with it (esp_hw_support/sleep_console.c). An enumerated CDC console does
// not survive that, and Arduino's HWCDC registers nothing with esp_pm to prevent
// it the way the IDF's own driver would.
bool IRAM_ATTR skipLightSleepForUsbConsole() { return usbConsoleUp; }
#endif

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
// IDLE task context: no logging, no allocation, no blocking call. Two adds.
// `sleepTimeUs` is the ACTUAL slept time on the exit callback (the enter
// callback would hand us the requested time, which is not the same number when
// something wakes the chip early — and waking early is the normal case here).
esp_err_t onLightSleepExit(const int64_t sleepTimeUs, void*) {
  if (sleepTimeUs > 0) {
    portENTER_CRITICAL(&sleepStatsLock);
    sleepAccumUs += static_cast<uint64_t>(sleepTimeUs);
    sleepAccumCount++;
    portEXIT_CRITICAL(&sleepStatsLock);
  }
  return ESP_OK;
}
#endif

}  // namespace

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  mainLoopTask = xTaskGetCurrentTaskHandle();  // Arduino runs setup() on the loop task
  normalFreq = getCpuFrequencyMhz();
  sleepMutex = xSemaphoreCreateMutex();
  assert(sleepMutex != nullptr);

  // Arm the flash-pad battery latch hold BEFORE the power manager is configured,
  // and never release it while running.
  //
  // This is the one thing that makes automatic light sleep survivable on an
  // Xteink board at all. From the esp_pm_configure() below onwards the FreeRTOS
  // idle task can enter light sleep at any moment, from code that has never
  // heard of this pin — and the IDF flash-leakage workaround pulls DIO-unused
  // flash pads low on every light-sleep entry, which on those boards releases
  // the battery latch and hard-powers-off the device. The hold survives sleep
  // and the wake path, so arming it once here covers every sleep the idle task
  // will ever take. holdBatteryLatchForSleep() is a no-op on other boards.
  holdBatteryLatchForSleep();

  const struct {
    esp_pm_lock_handle_t* handle;
    const char* name;
  } locks[] = {{&perfLock, "render"}, {&wifiLock, "wifi"}, {&activityLock, "activity"}};
  for (const auto& lock : locks) {
    const esp_err_t err = esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX, 0, lock.name, lock.handle);
    if (err != ESP_OK) {
      LOG_ERR("PWR", "esp_pm_lock_create(%s) failed: %d", lock.name, static_cast<int>(err));
      *lock.handle = nullptr;
    }
  }

  // max stays at whatever the build boots at (160 on the C3, 240 on the S3
  // boards): DFS is meant to change when the chip is idle, not to cap what it
  // can do when it is not. min is LOW_POWER_FREQ, which is 80 on every target so
  // that APB never moves -- the APB audit at the top of this file is green only
  // because of that number, so it is not a knob. The calibration knob for this
  // change is IDLE_ACTIVITY_HOLD_MS.
  const esp_pm_config_t pmConfig = {
      .max_freq_mhz = normalFreq,
      .min_freq_mhz = LOW_POWER_FREQ,
      .light_sleep_enable = true,
  };
  const esp_err_t err = esp_pm_configure(&pmConfig);
  if (err != ESP_OK) {
    // Not fatal: the chip keeps running at max_freq_mhz with no DFS and no
    // automatic sleep, which is the behaviour of a build with PM compiled out.
    // Logged loudly because the sleep ratio would then read 0.0% forever and
    // that reads identically to "PM is on and never finds a window".
    LOG_ERR("PWR", "esp_pm_configure(max %d, min %d) failed: %d", normalFreq, LOW_POWER_FREQ, static_cast<int>(err));
    return;
  }
  LOG_INF("PWR", "esp_pm: %d-%d MHz, light sleep on", LOW_POWER_FREQ, normalFreq);

#if CONFIG_PM_LIGHT_SLEEP_CALLBACKS
  esp_pm_sleep_cbs_register_config_t cbs = {};
  cbs.exit_cb = onLightSleepExit;
  const esp_err_t cbErr = esp_pm_light_sleep_register_cbs(&cbs);
  if (cbErr != ESP_OK) {
    // Only costs the readout, not the power saving.
    LOG_ERR("PWR", "sleep stats unavailable: %d", static_cast<int>(cbErr));
  }
#endif

#if CONFIG_PM_ENABLE && CONFIG_FREERTOS_USE_TICKLESS_IDLE
  // Slot budget: the IDF gives out PERIPH_SKIP_LIGHT_SLEEP_NO == 2
  // (esp_pm/pm_impl.c). This takes one. esp_wifi_init() takes the other for its
  // TSF callback, and treats a failure there as fatal to WiFi init, so nothing
  // else in this firmware may register one. The second WiFi registration exists
  // only under CONFIG_ESP_WIFI_ENHANCED_LIGHT_SLEEP, which is default n and
  // depends on SOC_PM_SUPPORT_BEACON_WAKEUP -- off on both targets here.
  const esp_err_t usbCbErr = esp_pm_register_skip_light_sleep_callback(skipLightSleepForUsbConsole);
  if (usbCbErr != ESP_OK) {
    // Serious: without the veto, plugging in a cable and watching the log is
    // enough to drop the console mid-session.
    LOG_ERR("PWR", "usb skip-light-sleep callback unavailable: %d", static_cast<int>(usbCbErr));
  }
#endif
}

void HalPowerManager::setLock(const esp_pm_lock_handle_t lock, bool& held, const bool wanted) {
  if (lock == nullptr || held == wanted) return;
  const esp_err_t err = wanted ? esp_pm_lock_acquire(lock) : esp_pm_lock_release(lock);
  if (err != ESP_OK) {
    LOG_ERR("PWR", "pm lock %s failed: %d", wanted ? "acquire" : "release", static_cast<int>(err));
    return;
  }
  held = wanted;
}

void HalPowerManager::setUsbConsoleUp(const bool up) const { usbConsoleUp = up; }

void HalPowerManager::updateLocks(const HalGPIO& gpio, const unsigned long idleMs) {
  setUsbConsoleUp(gpio.isUsbConnectedCached());
  setLock(wifiLock, wifiLockHeld, WiFi.getMode() != WIFI_MODE_NULL);
  setLock(activityLock, activityLockHeld, idleMs < IDLE_ACTIVITY_HOLD_MS);
}

size_t HalPowerManager::formatPowerReport(char* const out, const size_t outLen) const {
  if (out == nullptr || outLen == 0) return 0;
  out[0] = '\0';

  uint64_t sleptUs = 0;
  uint32_t sleepCount = 0;
  portENTER_CRITICAL(&sleepStatsLock);
  sleptUs = sleepAccumUs;
  sleepCount = sleepAccumCount;
  portEXIT_CRITICAL(&sleepStatsLock);

  // Only the locks this firmware owns. The IDF's own locks (WiFi, flash, the
  // drivers) are not enumerable without CONFIG_PM_PROFILING, which carries a
  // permanent runtime cost for a debug-only readout; turn that on and call
  // esp_pm_dump_locks() if a lock outside this list is ever suspected.
  char lockNames[40] = {0};
  const struct {
    bool held;
    const char* name;
  } locks[] = {{isPerfLockHeld(), "render"},
               // Not a PM lock; the skip-light-sleep veto. Listed alongside them
               // because it has the same effect on the ratio above and is the
               // single commonest reason for a session reading 0.0%.
               {usbConsoleUp, "usb"},
               {wifiLockHeld, "wifi"},
               {activityLockHeld, "activity"}};
  for (const auto& lock : locks) {
    if (!lock.held) continue;
    if (lockNames[0] != '\0') strncat(lockNames, ",", sizeof(lockNames) - strlen(lockNames) - 1);
    strncat(lockNames, lock.name, sizeof(lockNames) - strlen(lockNames) - 1);
  }

  const int written = power_report::format(out, outLen, static_cast<unsigned>(getCpuFrequencyMhz()), sleptUs,
                                           static_cast<uint64_t>(esp_timer_get_time()), sleepCount, lockNames);
  if (written <= 0) return 0;
  return static_cast<size_t>(written) < outLen ? static_cast<size_t>(written) : outLen - 1;
}

void HalPowerManager::startDeepSleep() const {
  // Hold the performance lock for the whole of this function. It is not about
  // speed: a CPU_FREQ_MAX lock keeps esp_pm out of PM_MODE_LIGHT_SLEEP, and the
  // wake-source disarm below is only safe if nothing re-arms one behind it.
  // Something would: deepSleepUntilPowerButton() starts by spinning on delay(50)
  // until the button is released, which blocks this task, and a blocked task is
  // exactly the condition the idle task light-sleeps on -- re-arming the timer
  // wake each time it does.
  const Lock stayAwake;

#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (hasFlashPadBatteryLatch()) {
    // GPIO13 is connected to the battery latch MOSFET. Keeping it low powers the MCU off on
    // battery, while the SDK wake source still handles USB power.
    //
    // The hold must be released first: holdBatteryLatchForSleep() pins this pad HIGH for
    // every light sleep and leaves the hold enabled while running, so without gpio_hold_dis()
    // the pad keeps its high level, the battery stays latched, and the device never powers
    // off. Release and drive low are symmetric with that hold, and therefore cover every
    // device the hold covers -- previously this ran on X4 only, because nothing drove the pad
    // high. Upstream #2525 drives it low on X3 as well.
    gpio_hold_dis(GPIO_BATTERY_LATCH);
    gpio_set_direction(GPIO_BATTERY_LATCH, GPIO_MODE_OUTPUT);
    gpio_set_level(GPIO_BATTERY_LATCH, 0);
    gpio_hold_en(GPIO_BATTERY_LATCH);
  }
#endif

  // Hold every configured power-latch pin HIGH through deep sleep. These are
  // keep-alive enables (the X4 Pro's master peripheral rail on GPIO1, the
  // Sticky's PWR_HOLD/PWR_LOCK): deepSleep() isolates all pads
  // (esp_sleep_config_gpio_isolate), so a latch without an armed hold loses its
  // output driver and floats — on the X4 Pro the latch drops as soon as
  // external power leaves (serial/pogo adapter unplugged), and the next power-
  // button press cold-boots instead of fast-waking. holdPowerRails() asserted
  // the latches at boot but arms no sleep hold; arm it here instead. Skips
  // GPIO13 on an Xteink board: it IS power.latch0 there, and the block above drives
  // it LOW on purpose (battery power-off on the X4, card rail off on the X3).
  // Upstream names that pin XTEINK_C3_GPIO13; here it is GPIO_BATTERY_LATCH, guarded
  // by the same isXteinkDevice() test the block above uses.
  for (const int8_t pin : {BoardConfig::ACTIVE.power.latch0, BoardConfig::ACTIVE.power.latch1}) {
    if (pin < 0) continue;
    if (hasFlashPadBatteryLatch() && static_cast<gpio_num_t>(pin) == GPIO_BATTERY_LATCH) continue;
    const auto g = static_cast<gpio_num_t>(pin);
    // Release any surviving pad hold first: a held pad silently ignores the
    // drive below (same trap as the GPIO13 block above).
    gpio_hold_dis(g);
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
    gpio_hold_en(g);
  }

  // Cut the gated peripheral rails (touch/SD/EPD on boards like the Sticky) and
  // hold the enables off through deep sleep — otherwise the GT911 and SD card
  // stay powered all through "off" and drain the battery. No-op on boards with
  // no switched rails (X4/X3). Trade-off: no touch-to-wake; wake is the power
  // button. Must run after display.deepSleep() so the panel controller gets its
  // deep-sleep command while its rail is still up (enterDeepSleep() in main.cpp
  // guarantees that ordering).
  freeink::PowerManager::powerDownRailsForSleep();

  // Disarm every wake source before handing over, because esp_deep_sleep_start()
  // honours all of them and the power manager leaves one armed.
  //
  // With automatic light sleep on, esp_pm calls esp_sleep_enable_timer_wakeup()
  // on every single sleep it takes (esp_pm/pm_impl.c, vApplicationSleep) and
  // never disables it again; esp_pm_configure() arms one at boot too. So by the
  // time the reader is switched off, ESP_SLEEP_WAKEUP_TIMER is enabled with a
  // few milliseconds left on it. deepSleepUntilPowerButton() adds the button
  // wake and clears nothing, so without this line the device would wake by
  // itself a few milliseconds after being powered off -- and keep doing it.
  // The Lock at the top of this function is what stops the timer being re-armed
  // between here and the sleep itself.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);

  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

bool HalPowerManager::onEinkBusyWaitSlice(const int8_t busyPin, const uint8_t busyLevel) {
  // Light sleep drops a WiFi association and kills an enumerated USB-CDC link.
  // updateLocks() already keeps the power manager awake for both, but this
  // sleeper is not the power manager's, so it has to make the same check for
  // itself. No LOG here — this runs ~50x/s mid-refresh.
  if (WiFi.getMode() != WIFI_MODE_NULL || gpio.isUsbConnectedCached()) {
    return false;
  }

  // Mid-debounce: committing needs a second sample, so let the SDK's short poll
  // delay run instead of halting the chip. Same guard the main loop applies to
  // its own idle wait. Unsynchronized like the checks above; stale costs at most
  // one slice.
  if (gpio.isDebouncePending()) {
    return false;
  }

  xSemaphoreTake(sleepMutex, portMAX_DELAY);

  // Wake the instant BUSY leaves its active level (refresh complete). Level
  // wake (not edge) means an already-completed refresh returns immediately
  // instead of sleeping a full slice. The timer bound only exists so the main
  // loop gets scheduling windows to keep sampling the ADC-ladder buttons.
  const auto pin = static_cast<gpio_num_t>(busyPin);
  gpio_wakeup_enable(pin, busyLevel == HIGH ? GPIO_INTR_LOW_LEVEL : GPIO_INTR_HIGH_LEVEL);
  // Also wake the instant the power button is pressed, so the main loop's poll
  // sees even sub-slice taps mid-refresh. The wake is only an early poll:
  // update()'s debounce still decides whether it was a real press, so a misread
  // around the sleep transition costs one extra wake blip, never a phantom press.
  const int8_t powerPin = BoardConfig::ACTIVE.input.power;
  if (powerPin >= 0) {
    gpio_wakeup_enable(static_cast<gpio_num_t>(powerPin),
                       BoardConfig::ACTIVE.input.powerActiveHigh ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(BUSY_SLEEP_SLICE_MS) * 1000ULL);

  // Battery-latch hold: the IDF flash-leakage workaround would drop the latch
  // pad on sleep entry and hard-power-off the device. begin() already armed it
  // for the whole session; repeating it here is free and keeps this sleeper
  // correct on its own terms rather than by depending on boot order.
  holdBatteryLatchForSleep();

  const esp_err_t err = esp_light_sleep_start();

  // Disarm everything armed above; an armed source persisting into
  // startDeepSleep() would wake the device on USB power. Also clear the LEVEL
  // interrupt types, which gpio_wakeup_disable() leaves behind: an asserted
  // level feeding the shared GPIO ISR service with no per-pin handler registered
  // re-enters the ISR forever and livelocks the CPU.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  gpio_wakeup_disable(pin);
  gpio_set_intr_type(pin, GPIO_INTR_DISABLE);
  if (powerPin >= 0) {
    gpio_wakeup_disable(static_cast<gpio_num_t>(powerPin));
    gpio_set_intr_type(static_cast<gpio_num_t>(powerPin), GPIO_INTR_DISABLE);
  }

  xSemaphoreGive(sleepMutex);

  if (err != ESP_OK) {
    return false;  // e.g. ESP_ERR_SLEEP_REJECT — fall back to the SDK's poll delay
  }

  // Yield until the equal-priority main loop finishes an iteration, not just one
  // tick: one tick is less awake CPU than an iteration needs, so we would re-halt
  // the chip mid-poll. Capped so a loop parked on SD I/O can't hold the panel awake.
  const uint32_t seenIterations = mainLoopIterations;
  // Skipped entirely when the loop cannot make progress and the yield would just
  // burn the cap awake: parked in requestUpdateAndWait() on this very render;
  // never started (setup-time paints, iterations still 0); or this task IS the
  // loop task (direct displayBuffer calls from setup()/loop() busy-wait here).
  const bool loopCanPoll = !mainLoopBlocked && seenIterations != 0 && xTaskGetCurrentTaskHandle() != mainLoopTask;
  unsigned yieldTicks = 0;
  if (loopCanPoll) {
    for (; yieldTicks < SLICE_YIELD_MAX_TICKS && mainLoopIterations == seenIterations; ++yieldTicks) {
      vTaskDelay(1);
    }
  }
  return true;
}

uint16_t HalPowerManager::getBatteryMillivolts() const {
  static const BatteryMonitor battery;
  return battery.readMillivolts();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  if (BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0) {
    const unsigned long now = millis();
    if (_batteryLastPollMs != 0 && (now - _batteryLastPollMs) < BATTERY_POLL_MS) {
      return _batteryCachedPercent;
    }

    _batteryLastPollMs = now;
    uint16_t percent = 0;
    if (!battery.readPercentageChecked(percent)) {
      return _batteryCachedPercent;
    }
    _batteryCachedPercent = percent;
    return _batteryCachedPercent;
  }

  // smooth the battery %.
  if (_batteryCachedPercent == 0) {
    _batteryCachedPercent = 10 * battery.readPercentage();
  } else {
    _batteryCachedPercent = (_batteryCachedPercent * 9 + battery.readPercentage() * 10) / 10;
  }
  return _batteryCachedPercent / 10;
}

HalPowerManager::Lock::Lock() {
  if (powerManager.perfLock == nullptr) return;  // PM lock creation failed at boot; nothing to hold
  const esp_err_t err = esp_pm_lock_acquire(powerManager.perfLock);
  if (err != ESP_OK) {
    LOG_ERR("PWR", "perf lock acquire failed: %d", static_cast<int>(err));
    return;
  }
  valid = true;
  // Bumped after the acquire succeeds, so isPerfLockHeld() never claims a lock
  // the chip is not actually holding.
  powerManager.perfLockDepth.fetch_add(1, std::memory_order_relaxed);
}

HalPowerManager::Lock::~Lock() {
  if (!valid) return;
  powerManager.perfLockDepth.fetch_sub(1, std::memory_order_relaxed);
  esp_pm_lock_release(powerManager.perfLock);
}
