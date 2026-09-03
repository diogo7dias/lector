#include "FirmwareSwitchAudit.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_ota_ops.h>
#include <nvs.h>

namespace firmware_flash {

namespace {
constexpr const char* kNamespace = "fwswitch";
constexpr const char* kKeyAddress = "addr";
constexpr const char* kKeySize = "size";
constexpr const char* kLogPath = "/lector-firmware-update.log";
bool s_switchRolledBack = false;
}  // namespace

void recordPendingSwitch(uint32_t address, size_t imageSize) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;
  nvs_set_u32(h, kKeyAddress, address);
  nvs_set_u32(h, kKeySize, static_cast<uint32_t>(imageSize));
  nvs_commit(h);
  nvs_close(h);
}

void auditPendingSwitch(const char* version) {
  nvs_handle_t h;
  if (nvs_open(kNamespace, NVS_READWRITE, &h) != ESP_OK) return;

  uint32_t intended = 0;
  uint32_t imageSize = 0;
  const bool havePending = nvs_get_u32(h, kKeyAddress, &intended) == ESP_OK;
  nvs_get_u32(h, kKeySize, &imageSize);
  if (havePending) {
    // One shot: whatever the outcome, the record does not survive this boot.
    nvs_erase_key(h, kKeyAddress);
    nvs_erase_key(h, kKeySize);
    nvs_commit(h);
  }
  nvs_close(h);
  if (!havePending) return;

  const esp_partition_t* running = esp_ota_get_running_partition();
  const uint32_t runningAddress = running ? running->address : 0;
  if (running && runningAddress == intended) {
    LOG_INF("FLASH", "firmware switch to 0x%06X took effect", static_cast<unsigned>(intended));
    return;
  }

  s_switchRolledBack = true;
  const std::string line = formatSwitchFailedLine(version, intended, runningAddress, imageSize);
  LOG_ERR("FLASH", "%s", line.c_str());

  HalFile file;
  if (!Storage.openFileForWrite("FLASH", kLogPath, file) || !file) return;
  file.seek(file.fileSize());  // append rather than overwrite earlier attempts
  file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
  file.close();
}

bool didPreviousSwitchRollBack() { return s_switchRolledBack; }

}  // namespace firmware_flash
