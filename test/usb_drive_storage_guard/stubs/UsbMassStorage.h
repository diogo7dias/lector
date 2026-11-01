#pragma once
#include <SDCardManager.h>

#include <cstdint>
namespace freeink {
enum class UsbMassStorageState : uint8_t { Idle, WaitingForHost, Connected, Accessed, Ejected, Disconnected, IoError };
class UsbMassStorage {
 public:
  bool begin(FsBlockDeviceInterface*) { return true; }
  void end() {}
  UsbMassStorageState state() const { return UsbMassStorageState::WaitingForHost; }
  bool disconnectHost() const { return true; }
  bool active() const { return false; }
};
}  // namespace freeink

// TinyUSB device state, read by HalStorage::usbDriveHostSuspended().
extern "C" inline bool tud_mounted(void) { return false; }
extern "C" inline bool tud_suspended(void) { return false; }
