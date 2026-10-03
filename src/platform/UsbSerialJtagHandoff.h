#pragma once

// Releases the ESP32-S3 USB-OTG PHY and reconnects the hardware USB Serial/JTAG
// peripheral before a restart, so the console and flashing work again after USB
// Drive. Mirrors Arduino-ESP32 3.3.7's private usb_switch_to_cdc_jtag(); recheck
// it when the framework version moves. Built only with FREEINK_CAP_USB_MSC.
void handoffUsbOtgToSerialJtag();
