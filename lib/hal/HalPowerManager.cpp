#include "HalPowerManager.h"

#include <BoardConfig.h>
#include <Logging.h>
#include <PowerManager.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <soc/rtc.h>
#include <soc/soc_caps.h>
#if SOC_USB_SERIAL_JTAG_SUPPORTED
#include <driver/usb_serial_jtag.h>
#endif
#if SOC_PM_SUPPORT_EXT0_WAKEUP
#include <driver/rtc_io.h>
#endif
#if CONFIG_IDF_TARGET_ESP32S3
#include <driver/usb_serial_jtag.h>
#include <esp_private/mspi_timing_tuning.h>
#include <esp_timer.h>
#endif

#include <cassert>

#include "HalGPIO.h"
#include "HalStorage.h"

HalPowerManager powerManager;  // Singleton instance

// On the S3 the flash and PSRAM bus takes its clock from the CPU's source, and its 80 MHz
// timing is tuned against the PLL. Under 80 MHz the CPU runs from the crystal, so the bus
// goes to its untuned low-speed mode before the switch and returns after the switch back.
// Same order as ESP-IDF's frequency scaling (esp_clk_utils_mspi_speed_mode_sync_*), which
// setCpuFrequencyMhz() does not do by itself.
#if CONFIG_IDF_TARGET_ESP32S3
// The bus switch freezes the cache and does not mask interrupts itself: ESP-IDF calls it
// with them masked, because an interrupt handler that lives in flash would hang on the
// frozen cache.
static void setMemoryBusLowSpeed(const bool low) {
  static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  portENTER_CRITICAL(&mux);
  mspi_timing_change_speed_mode_cache_safe(low);
  portEXIT_CRITICAL(&mux);
}
#endif

static bool isUsbSerialJtagActive() {
#if SOC_USB_SERIAL_JTAG_SUPPORTED
  return usb_serial_jtag_is_connected();
#else
  return false;
#endif
}

// setCpuFrequencyMhz() calls rtc_clk_cpu_freq_set_config_fast(), which leaves the
// 480 MHz BBPLL running on XTAL clocks. When disableBbpll is true, call
// rtc_clk_cpu_freq_set_config() instead so the PLL actually powers down; restoring
// >= 80 MHz via setCpuFrequencyMhz() falls back to rtc_clk_cpu_freq_set_config()
// automatically when the PLL is off.
static bool applyCpuFreqMhz(const int mhz, const bool disableBbpll) {
  if (!disableBbpll) {
    return setCpuFrequencyMhz(mhz);
  }
  rtc_cpu_freq_config_t current{};
  rtc_clk_cpu_freq_get_config(&current);
  if (current.freq_mhz == static_cast<uint32_t>(mhz)) {
    return true;
  }
  rtc_cpu_freq_config_t target{};
  if (!rtc_clk_cpu_freq_mhz_to_config(static_cast<uint32_t>(mhz), &target)) {
    return false;
  }
  rtc_clk_cpu_freq_set_config(&target);
  return true;
}

static bool setCpuClock(const int mhz, const bool disableBbpll) {
#if CONFIG_IDF_TARGET_ESP32S3
  if (mhz < 80) {
    setMemoryBusLowSpeed(true);
    if (applyCpuFreqMhz(mhz, disableBbpll)) return true;
    setMemoryBusLowSpeed(false);
    return false;
  }
  if (!applyCpuFreqMhz(mhz, false)) return false;
  setMemoryBusLowSpeed(false);
  return true;
#else
  return applyCpuFreqMhz(mhz, disableBbpll);
#endif
}

void HalPowerManager::holdNormalSpeedForStorage() {
#if CONFIG_IDF_TARGET_ESP32S3
  // Unlocked read: the clock only goes down under the storage mutex, which the caller holds.
  if (!powerState.isLowPower) return;
  storageWokeAtMs = millis();
  setPowerSaving(false);
#endif
}

// GPIO13 is the flash SPIWP pad (unused in DIO flash mode), rewired on the Xteink boards to
// different things per board:
//   X4: the battery-latch MOSFET gate. High keeps the battery connected, low powers off.
//   X3: the SD card's power rail enable, active high. It is NOT the off switch there.
// Driving it low into deep sleep is right for both: X4 powers off, X3 stops the card draining.
// On the X4 Pro it is the display chip select, hence the isXteinkDevice() guard.
static constexpr gpio_num_t GPIO_BATTERY_LATCH = GPIO_NUM_13;

void HalPowerManager::begin() {
  if (BoardConfig::ACTIVE.batteryAdc >= 0) {
    pinMode(BoardConfig::ACTIVE.batteryAdc, INPUT);
  }
  normalFreq = getCpuFrequencyMhz();
  modeMutex = xSemaphoreCreateMutex();
  assert(modeMutex != nullptr);
}

void HalPowerManager::setPowerSaving(bool enabled) {
  if (normalFreq <= 0) {
    return;  // invalid state
  }

  const bool wifiActive = (WiFi.getMode() != WIFI_MODE_NULL);
  if (wifiActive) {
    // Wifi is active, force disabling power saving
    enabled = false;
  }
  const bool usbConnected = gpio.isUsbConnectedCached() || isCharging();
  const bool usjActive = isUsbSerialJtagActive();

  // Held across the check and the switch: a Lock taken in between would find full speed, do
  // nothing, and then run at LOW_POWER_FREQ until the next key press.
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  const LockMode mode = currentLockMode;

  if (mode == None && enabled && !powerState.isLowPower) {
#if CONFIG_IDF_TARGET_ESP32S3
    // Not in the middle of a card transfer, and not straight after one: work that keeps
    // reading the card would otherwise bounce the clock on every loop pass.
    if (storageWokeAtMs != 0 && millis() - storageWokeAtMs < IDLE_POWER_SAVING_MS) {
      xSemaphoreGive(modeMutex);
      return;
    }
    if (!Storage.tryLock()) {
      xSemaphoreGive(modeMutex);
      return;
    }
#endif
    const bool disableBbpll = power_clock::shouldDisableBbpll(LOW_POWER_FREQ, wifiActive, usbConnected, usjActive);
    // INF, not DBG: the released X4 Pro image logs at level 1, and its sim suite checks
    // that an idle device gets here and comes back. Logged before dropping so HWCDC
    // still has the 48 MHz USB clock if active.
    LOG_INF("PWR", "Going to low-power mode (%d MHz, bbpll=%s)", LOW_POWER_FREQ, disableBbpll ? "off" : "on");
    const bool lowered = setCpuClock(LOW_POWER_FREQ, disableBbpll);
    if (lowered) {
      powerState.isLowPower = true;
      bbpllDisabled = disableBbpll;
    }
#if CONFIG_IDF_TARGET_ESP32S3
    Storage.unlock();
#endif
    if (!lowered) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", LOW_POWER_FREQ);
      xSemaphoreGive(modeMutex);
      return;
    }

  } else if ((!enabled || mode != None || (bbpllDisabled && (usbConnected || usjActive))) && powerState.isLowPower) {
#if LECTOR_IDLE_LIGHT_SLEEP
    if (lightSleepSlices > 0) {
      LOG_INF("PWR", "Light sleep: slices=%lu slept_ms=%llu gpio_wakes=%lu",
              static_cast<unsigned long>(lightSleepSlices),
              static_cast<unsigned long long>(lightSleepTotalUs / 1000ULL),
              static_cast<unsigned long>(lightSleepGpioWakes));
      lightSleepSlices = 0;
      lightSleepTotalUs = 0;
      lightSleepGpioWakes = 0;
    }
#endif
    if (!setCpuClock(normalFreq, false)) {
      LOG_DBG("PWR", "Failed to set CPU frequency = %d MHz", normalFreq);
      xSemaphoreGive(modeMutex);
      return;
    }
    powerState.onRestoredToNormal();
    bbpllDisabled = false;
    LOG_INF("PWR", "Restoring normal CPU frequency");
  }

  // Otherwise, no change needed
  xSemaphoreGive(modeMutex);
}

void HalPowerManager::beginBusyWait() {
  if (normalFreq <= 0 || modeMutex == nullptr) return;
  const bool wifiActive = (WiFi.getMode() != WIFI_MODE_NULL);
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  if (!powerState.shouldLowerOnBusyBegin(wifiActive)) {
    xSemaphoreGive(modeMutex);
    return;
  }
#if CONFIG_IDF_TARGET_ESP32S3
  if (!Storage.tryLock()) {
    xSemaphoreGive(modeMutex);
    return;
  }
  // USB Drive serves the host through TinyUSB between its card transfers, when the storage
  // lock is free: the clock stays up for as long as the card is lent (main.cpp's exclusive
  // loop holds it up for the same reason).
  if (Storage.usbDriveState() != UsbDriveState::Unsupported) {
    Storage.unlock();
    xSemaphoreGive(modeMutex);
    return;
  }
#endif
  LOG_INF("PWR", "Busy-wait low-power (%d MHz)", LOW_POWER_FREQ);
  // The PLL stays up: the wait is short, and the next thing the panel needs is the full clock.
  const bool lowered = setCpuClock(LOW_POWER_FREQ, false);
  if (lowered) powerState.onBusyLowered();
#if CONFIG_IDF_TARGET_ESP32S3
  Storage.unlock();
#endif
  xSemaphoreGive(modeMutex);
}

void HalPowerManager::endBusyWait() {
  if (normalFreq <= 0 || modeMutex == nullptr) return;
  xSemaphoreTake(modeMutex, portMAX_DELAY);
  if (powerState.takeRestoreOnBusyEnd()) {
    if (setCpuClock(normalFreq, false)) {
      powerState.onRestoredToNormal();
      LOG_DBG("PWR", "Restored normal CPU frequency after busy-wait");
    } else {
      LOG_DBG("PWR", "Failed to restore CPU frequency = %d MHz after busy-wait", normalFreq);
    }
  }
  xSemaphoreGive(modeMutex);
}

bool HalPowerManager::idleLightSleepSlice(const bool renderIdle, const bool touchHeld, const bool touchAsleep,
                                          const uint32_t maxSliceMs) {
#if CONFIG_IDF_TARGET_ESP32S3 && LECTOR_IDLE_LIGHT_SLEEP
  if (normalFreq <= 0 || modeMutex == nullptr) return false;
  const uint32_t sliceMs = idle_sleep::sliceDurationMs(touchAsleep, maxSliceMs);
  if (sliceMs < 5) return false;

  idle_sleep::Conditions cond;
  cond.wifiActive = (WiFi.getMode() != WIFI_MODE_NULL);
  cond.usbConnected = gpio.isUsbConnectedCached() || isCharging();
  cond.usbSerialJtagActive = usb_serial_jtag_is_connected();
  cond.renderIdle = renderIdle;
  cond.touchHeld = touchHeld;
  cond.touchAsleep = touchAsleep;
  cond.rawInputActive = gpio.rawInputActive();
  // Quick pre-check before taking mutexes.
  cond.gpioWakeArmed = true;
  if (!idle_sleep::canLightSleep(cond)) return false;

  if (xSemaphoreTake(modeMutex, 0) != pdTRUE) return false;
  cond.powerLockHeld = (currentLockMode != None);
  if (!powerState.isLowPower || cond.powerLockHeld || !Storage.tryLock()) {
    xSemaphoreGive(modeMutex);
    return false;
  }

  // Collect active-low digital button GPIOs (X4 Pro: GPIO0 up, GPIO7 down, GPIO3 power).
  gpio_num_t armedPins[8] = {};
  uint8_t armedCount = 0;
  const auto armPin = [&](const int8_t pin, const gpio_int_type_t intrType) {
    if (pin < 0 || armedCount >= 8) return;
    const auto g = static_cast<gpio_num_t>(pin);
    for (uint8_t i = 0; i < armedCount; ++i) {
      if (armedPins[i] == g) return;
    }
    if (gpio_wakeup_enable(g, intrType) == ESP_OK) {
      armedPins[armedCount++] = g;
    }
  };

  const auto& in = BoardConfig::ACTIVE.input;
  if (BoardConfig::ACTIVE.inputStyle == BoardConfig::InputStyle::DigitalButtons) {
    for (const int8_t pin : {in.up, in.down, in.power, in.back, in.confirm, in.left, in.right}) {
      armPin(pin, GPIO_INTR_LOW_LEVEL);
    }
  } else {
    armPin(in.power, GPIO_INTR_LOW_LEVEL);
  }
  const uint8_t buttonPinsArmed = armedCount;

  // Arm GT911 INT (GPIO10) only while touch is awake. When touch is asleep,
  // InputManager drives GPIO10 LOW as an output so arming it would self-wake.
  const auto& touch = BoardConfig::ACTIVE.touch;
  if (!touchAsleep && touch.irq >= 0 && touch.controller == BoardConfig::TouchController::Gt911) {
    const int idleLevel = digitalRead(touch.irq);
    const gpio_int_type_t intrType =
        idle_sleep::touchWakeHighLevel(idleLevel) ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL;
    armPin(touch.irq, intrType);
  }

  cond.gpioWakeArmed = (buttonPinsArmed > 0);
  cond.rawInputActive = gpio.rawInputActive();
  bool slept = false;
  if (idle_sleep::canLightSleep(cond) && esp_sleep_enable_gpio_wakeup() == ESP_OK &&
      esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(sliceMs) * 1000ULL) == ESP_OK) {
    const int64_t t0 = esp_timer_get_time();
    const esp_err_t err = esp_light_sleep_start();
    const int64_t dt = esp_timer_get_time() - t0;
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    if (err == ESP_OK) {
      slept = true;
      // Light sleep switches the PLL off and comes back on the crystal with it still off.
      bbpllDisabled = true;
      lightSleepSlices++;
      if (dt > 0) lightSleepTotalUs += static_cast<uint64_t>(dt);
      if (cause == ESP_SLEEP_WAKEUP_GPIO) lightSleepGpioWakes++;
    }
  }

  // Always tear down wake sources and clear level interrupt types before returning:
  // leaving GPIO_INTR_{LOW,HIGH}_LEVEL armed livelocks the shared GPIO ISR service.
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  for (uint8_t i = 0; i < armedCount; ++i) {
    gpio_wakeup_disable(armedPins[i]);
    gpio_set_intr_type(armedPins[i], GPIO_INTR_DISABLE);
  }

  Storage.unlock();
  xSemaphoreGive(modeMutex);
  return slept;
#else
  (void)renderIdle;
  (void)touchHeld;
  (void)touchAsleep;
  (void)maxSliceMs;
  return false;
#endif
}

// X4 Pro charger STAT line: GPIO21, driven HIGH while charging. Not in BoardConfig for this board; the pin
// is the one upstream CrossPoint #4454 measured on an X4 Pro. It is the only sign of a cable the board gives.
static constexpr gpio_num_t X4PRO_CHARGE_STATUS = GPIO_NUM_21;

bool HalPowerManager::supportsChargeWake() {
#if SOC_PM_SUPPORT_EXT0_WAKEUP
  return BoardConfig::isX4Pro();
#else
  return false;
#endif
}

bool HalPowerManager::isCharging() {
  // Only the X4 Pro has this line; elsewhere GPIO21 is another pin (UART0 TX on the C3) and must stay untouched.
  if (!supportsChargeWake()) return false;
  // A weak pull-down, so a line the charger leaves floating reads as not charging.
  pinMode(X4PRO_CHARGE_STATUS, INPUT_PULLDOWN);
  return digitalRead(X4PRO_CHARGE_STATUS) == HIGH;
}

void HalPowerManager::startDeepSleep() const {
#ifdef ENABLE_SERIAL_LOG
  // Tear down HWCDC so the host sees a clean disconnect and the peripheral
  // doesn't hold power domains that interfere with USB-powered GPIO wake.
  // logSerial is the raw HWCDC reference; Serial is the MySerialImpl proxy
  // (which doesn't expose end()).
  logSerial.end();
#endif

#if !SOC_PM_SUPPORT_EXT1_WAKEUP
  if (gpio.isXteinkDevice()) {
    // GPIO13 is connected to the battery latch MOSFET. Keeping it low powers the MCU off on
    // battery, while the SDK wake source still handles USB power.
    // Release any surviving pad hold first: a held pad silently ignores the drive.
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
    if (gpio.isXteinkDevice() && static_cast<gpio_num_t>(pin) == GPIO_BATTERY_LATCH) continue;
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

#if SOC_PM_SUPPORT_EXT0_WAKEUP
  // Also wake when a cable starts a charge. Not while already charging: the level wake would fire at once and
  // loop sleep and boot, so a cable left in is simply not noticed (the power button still wakes).
  if (chargeWake && supportsChargeWake() && wake_classify::armsPlugInWake(true, isCharging())) {
    if (esp_sleep_enable_ext0_wakeup(X4PRO_CHARGE_STATUS, 1) == ESP_OK) {
      // ext0 needs the RTC peripherals powered, and the line held idle (low) so it reads as not charging.
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      rtc_gpio_pullup_dis(X4PRO_CHARGE_STATUS);
      rtc_gpio_pulldown_en(X4PRO_CHARGE_STATUS);
    } else {
      LOG_ERR("PWR", "Plug-in wake: ext0 on GPIO%d refused", static_cast<int>(X4PRO_CHARGE_STATUS));
    }
  }
#endif

  // Waits for the power button to be physically released (so holding it doesn't
  // immediately wake the device again), then arms the wake source and sleeps.
  freeink::PowerManager::deepSleepUntilPowerButton();
}

uint16_t HalPowerManager::getBatteryMillivolts() const {
  static const BatteryMonitor battery;
  return battery.readMillivolts();
}

uint16_t HalPowerManager::getBatteryPercentage() const {
  static const BatteryMonitor battery;
  const bool hasGauge = BoardConfig::ACTIVE.batteryGauge.gaugeAddr != 0;
  return _batteryPoll.get(hasGauge, millis(), [hasGauge](uint16_t& percent) {
    if (hasGauge) return battery.readPercentageChecked(percent);
    percent = battery.readPercentage();
    return true;
  });
}

HalPowerManager::Lock::Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  // Current limitation: only one lock at a time
  if (powerManager.currentLockMode != None) {
    LOG_DBG("PWR", "Lock already held, ignore");
    valid = false;
  } else {
    powerManager.currentLockMode = NormalSpeed;
    valid = true;
  }
  xSemaphoreGive(powerManager.modeMutex);
  if (valid) {
    // Immediately restore normal CPU frequency if currently in low-power mode
    powerManager.setPowerSaving(false);
  }
}

HalPowerManager::Lock::~Lock() {
  xSemaphoreTake(powerManager.modeMutex, portMAX_DELAY);
  if (valid) {
    powerManager.currentLockMode = None;
  }
  xSemaphoreGive(powerManager.modeMutex);
}
