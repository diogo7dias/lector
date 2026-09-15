#pragma once

#include <cstdint>

// Which device this is, as ONE value.
//
// Before this existed, "which device is this" had three unrelated spellings that each
// decided display behaviour, so there was no single predicate to grep for:
//   gpio.deviceIsX3()
//   BoardConfig::ACTIVE.displayController == UC8279 && !gpio.deviceIsX3()
//   BoardConfig::isX4Pro()
// All three are reads of a global singleton, so none of them is callable from a host
// test, and the rules that depend on them ("an X3 HALF is not one waveform", "the
// grayscale base is FULL on a UC8279 X4") could only be pinned by tests that grep the
// source text for the spelling of the guard.
//
// This struct is built once at boot from the hardware detect and then PASSED. A function
// that takes a DeviceProfile is pure and host-testable; a function that calls `gpio` is
// neither. It is a plain POD on purpose: no factory, no interface, no per-device
// subclass. One value, threaded down.
//
// hasTouch is a RUNTIME field and must stay one. The `default` PlatformIO environment
// builds ONE binary for both the keys-only boards (X3/X4) and the X4 Pro, so a
// compile-time split cannot tell them apart. See test/device_look/DeviceLookAuditTest.cpp.
struct DeviceProfile {
  bool isX3 = false;
  bool isX4Pro = false;
  // The panel controller this board carries. Kept as a bool rather than the BoardConfig
  // enum so this header stays dependency-free and host-includable; UC8279 is the only
  // controller any display rule currently branches on.
  bool controllerIsUc8279 = false;
  bool hasTouch = false;
};

// Reads the hardware detect (HalGPIO + BoardConfig) once. Valid only after gpio.begin(),
// which is what runs the detect and selects the active board.
DeviceProfile deviceProfileFromHardware();
