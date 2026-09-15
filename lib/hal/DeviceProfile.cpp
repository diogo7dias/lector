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
  profile.hasTouch = gpio.hasTouch();
  return profile;
}
