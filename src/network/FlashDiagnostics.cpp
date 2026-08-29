#include "FlashDiagnostics.h"

#include <HalStorage.h>
#include <Logging.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <spi_flash_mmap.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>

#include "OtaBootEntry.h"
#include "OtaBootSwitch.h"

namespace firmware_flash {

namespace {
constexpr const char* kPath = "/lector-flash-diagnostics.txt";

void append(const std::string& text) {
  HalFile file;
  if (!Storage.openFileForWrite("FLASH", kPath, file) || !file) {
    LOG_ERR("FLASH", "diagnostics: cannot open %s", kPath);
    return;
  }
  file.seek(file.fileSize());
  file.write(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  file.close();
}

std::string line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

std::string line(const char* fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  return std::string(buf);
}

// Every partition the bootloader can see, because the assumption this file
// exists to test is that a locked device's table is not the one Lector ships.
std::string describePartitions() {
  std::string out;
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  int apps = 0;
  while (it != nullptr) {
    const esp_partition_t* p = esp_partition_get(it);
    if (p->type == ESP_PARTITION_TYPE_APP) apps++;
    out += line("  %-8s type=%u subtype=0x%02X addr=0x%06X size=0x%06X\n", p->label, p->type, p->subtype,
                static_cast<unsigned>(p->address), static_cast<unsigned>(p->size));
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  out += line("  app partitions: %d\n", apps);
  return out;
}

// The two 32-byte otadata records, raw. The bootloader picks a slot from these
// alone, so if the device keeps booting the old firmware after a "complete"
// install, the answer is either here or in the table above.
std::string describeOtadata() {
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) return "  otadata: NOT FOUND\n";

  std::string out = line("  otadata addr=0x%06X size=0x%06X\n", static_cast<unsigned>(otadata->address),
                         static_cast<unsigned>(otadata->size));
  for (int i = 0; i < 2; ++i) {
    ota_boot::SelectEntry e = {};
    const size_t off = static_cast<size_t>(i) * SPI_FLASH_SEC_SIZE;
    if (off + sizeof(e) > otadata->size || esp_partition_read(otadata, off, &e, sizeof(e)) != ESP_OK) {
      out += line("  slot%d: unreadable\n", i);
      continue;
    }
    const bool crcOk = e.ota_seq != 0xFFFFFFFFu && e.crc == ota_boot::computeSeqCrc(e.ota_seq);
    out += line("  slot%d: seq=%u state=0x%08X crc=0x%08X crc_ok=%d\n", i, static_cast<unsigned>(e.ota_seq),
                static_cast<unsigned>(e.ota_state), static_cast<unsigned>(e.crc), crcOk ? 1 : 0);
  }
  return out;
}

std::string describeRunning() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (!run) return "  running: UNKNOWN\n";
  return line("  running: %s subtype=0x%02X addr=0x%06X size=0x%06X\n", run->label, run->subtype,
              static_cast<unsigned>(run->address), static_cast<unsigned>(run->size));
}

}  // namespace

void diagnosticsBeginAttempt(const char* version, const char* imagePath, const size_t imageSize) {
  std::string out = "=== install attempt ===\n";
  out += line("  firmware: %s\n", version ? version : "?");
  out += line("  image: %s size=%u\n", imagePath ? imagePath : "(over the air)", static_cast<unsigned>(imageSize));
  out += describeRunning();
  out += describePartitions();
  out += "  otadata before:\n";
  out += describeOtadata();
  append(out);
}

void diagnosticsEndAttempt(const uint32_t destAddress, const char* destLabel, const uint8_t destSubtype,
                           const bool switchOk) {
  const uint32_t destOtaIdx = static_cast<uint32_t>(destSubtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  std::string out = line("  dest: %s subtype=0x%02X addr=0x%06X ota_idx=%u switch_ok=%d\n",
                         destLabel ? destLabel : "?", destSubtype, static_cast<unsigned>(destAddress),
                         static_cast<unsigned>(destOtaIdx), switchOk ? 1 : 0);
  out += "  otadata after:\n";
  out += describeOtadata();
  append(out);
}

void diagnosticsRecordBoot(const char* version) {
  // Only once an install has been attempted: the first attempt creates the
  // file, and a reader that never tries to change firmware never writes to the
  // card at boot.
  if (!Storage.exists(kPath)) return;
  std::string out = "=== boot ===\n";
  out += line("  firmware: %s\n", version ? version : "?");
  out += describeRunning();
  out += describeOtadata();
  append(out);
}

}  // namespace firmware_flash
