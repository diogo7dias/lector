#pragma once

// Pure policy: what kind of boot is this?
//
// HalGPIO::getWakeupReason() reads the chip (reset reason, sleep wake cause, USB, board)
// and hands the plain values here. No Arduino or ESP-IDF includes, so every row is
// host-testable. See test/wake_classify.
//
// Why the Xteink X4 needs its own row: its lock drives the battery latch low
// (HalPowerManager::startDeepSleep), a full power cut, so every X4 unlock arrives as a
// cold boot (POWERON), never as a deep-sleep reset. The X3 and the X4 Pro wake from deep
// sleep proper (DEEPSLEEP). That is what coldBootImpliesPowerButton exists to tell apart.

#include <cstdint>

namespace wake_classify {

enum class WakeupReason { PowerButton, AfterFlash, AfterUSBPower, Other };

// The only reset reasons the decision tells apart. Everything else (software restart,
// panic, watchdog, brownout, ...) is Other.
enum class Reset : uint8_t { PowerOn, DeepSleep, Unknown, Other };

struct BootFacts {
  Reset reset = Reset::Other;
  // esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_UNDEFINED.
  bool wakeCauseReported = false;
  bool usbConnected = false;
  // Xteink-style power topology: the power button energizes the rail until firmware
  // latches it, so a no-USB POWERON can only be a still-held button boot, and plugging
  // USB into an off device should charge-sleep, not boot. Everything else boots on any
  // cold boot: boards with no USB detection at all would misread USB and post-flash
  // boots as battery button boots. HalGPIO::coldBootImpliesPowerButton() answers it.
  bool coldBootImpliesPowerButton = false;
};

inline constexpr WakeupReason classify(const BootFacts& f) {
  // Every deep-sleep exit is a power-button wake: the button is the only source
  // startDeepSleep() arms. The reported cause cannot be trusted to say so: an X4 Pro
  // trace (lector.exp.54) recorded a real button unlock as rst=DEEPSLEEP cause=TIMER,
  // and esp_sleep_get_wakeup_cause() reports TIMER ahead of every other bit. Classed as
  // Other, that unlock skipped the painted-face path and drew the boot splash before
  // the book. A wake without a press is still caught: verifyPowerButtonWakeup() sends
  // it back to sleep.
  if (f.reset == Reset::DeepSleep) return WakeupReason::PowerButton;
  if (f.wakeCauseReported) return WakeupReason::Other;
  if (f.reset == Reset::PowerOn && !f.usbConnected && f.coldBootImpliesPowerButton) return WakeupReason::PowerButton;
  if (f.reset == Reset::Unknown && f.usbConnected) return WakeupReason::AfterFlash;
  if (f.reset == Reset::PowerOn && f.usbConnected) return WakeupReason::AfterUSBPower;
  return WakeupReason::Other;
}

}  // namespace wake_classify
