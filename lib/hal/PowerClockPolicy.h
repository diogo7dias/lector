#pragma once

namespace power_clock {

// Below 80 MHz the CPU runs from XTAL instead of the 480 MHz BBPLL. Turning
// BBPLL off saves its analog draw, but Wi-Fi and native USB Serial/JTAG both
// need the PLL clock (48 MHz for USB PHY). Keep BBPLL running whenever Wi-Fi,
// external USB power, or an active USB Serial/JTAG host is present.
constexpr bool shouldDisableBbpll(const int targetMhz, const bool wifiActive, const bool usbConnected,
                                  const bool usbSerialJtagActive) {
  return targetMhz < 80 && !wifiActive && !usbConnected && !usbSerialJtagActive;
}

// Valid 40 MHz XTAL divisors (10, 20, 40 MHz) plus the lowest PLL step (80 MHz)
// for the compile-time LECTOR_LOW_POWER_FREQ_MHZ knob.
constexpr bool isValidLowPowerFreqMhz(const int mhz) { return mhz == 10 || mhz == 20 || mhz == 40 || mhz == 80; }

}  // namespace power_clock
