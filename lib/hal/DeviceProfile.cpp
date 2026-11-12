#include <BoardConfig.h>
#include <DeviceProfile.h>
#include <HalGPIO.h>

// The one place the three global spellings of "which device is this" are read. Everything
// downstream takes the resulting value as a parameter.
DeviceProfile deviceProfileFromHardware() {
  DeviceProfile profile;
  profile.isX3 = gpio.deviceIsX3();
  profile.isX4Pro = BoardConfig::isX4Pro();
  profile.controllerIsUc8279 = BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::UC8279;
  profile.controllerIsSsd1677 = BoardConfig::ACTIVE.displayController == BoardConfig::DisplayController::SSD1677;
  // Same test as the SDK's otpOnly() (Uc8279X4Driver.cpp): stock ships no gray LUT for 0x67.
  profile.panelHasGray = !(profile.controllerIsUc8279 && BoardConfig::ACTIVE.displayControllerVariant == 0x67);
  profile.hasTouch = gpio.hasTouch();
  return profile;
}
