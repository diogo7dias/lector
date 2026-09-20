#include "Diagnostics.h"

#include <Arduino.h>
#include <BoardConfig.h>
#include <DiagLog.h>
#include <HalClock.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <Logging.h>
#include <Memory.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_random.h>
#include <esp_system.h>
#include <spi_flash_mmap.h>

#include <cstdarg>
#include <cstdio>

#include "network/OtaBootEntry.h"
#include "network/OtaBootSwitch.h"

namespace diag {

namespace {
constexpr const char* kTag = "DIAG";

// Random per boot, so two attempts from two power cycles never share a tag
// even when the clock is unset on both. Not a device identifier.
uint16_t g_session = 0;
uint8_t g_attempt = 0;

uint16_t session() {
  if (g_session == 0) g_session = static_cast<uint16_t>(esp_random() | 1u);
  return g_session;
}

diaglog::Clock now() {
  uint16_t y;
  uint8_t mo, d, h, mi;
  uint32_t utc = 0;
  if (halClock.getDateTime(y, mo, d, h, mi)) utc = diaglog::utcFromCivil(y, mo, d, h, mi, 0);
  return {utc, static_cast<uint32_t>(millis() / 1000)};
}

const char* boardName() {
  switch (BoardConfig::ACTIVE.board) {
    case BoardConfig::Board::XteinkX4:
      return "X4";
    case BoardConfig::Board::XteinkX3:
      return "X3";
    case BoardConfig::Board::XteinkX3Uc8279:
      return "X3(UC8279)";
    case BoardConfig::Board::XteinkX4Pro:
      return "X4Pro";
    case BoardConfig::Board::Sticky:
      return "Sticky";
    default:
      return "other";
  }
}

const char* panelName() {
  switch (BoardConfig::ACTIVE.displayController) {
    case BoardConfig::DisplayController::SSD1677:
      return "SSD1677";
    case BoardConfig::DisplayController::UC8253:
      return "UC8253";
    case BoardConfig::DisplayController::UC8279:
      return "UC8279";
    case BoardConfig::DisplayController::UC8179:
      return "UC8179";
    default:
      return "other";
  }
}

const char* resetName(const esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:
      return "POWERON";
    case ESP_RST_EXT:
      return "EXT";
    case ESP_RST_SW:
      return "SW";
    case ESP_RST_PANIC:
      return "PANIC";
    case ESP_RST_INT_WDT:
      return "INT_WDT";
    case ESP_RST_TASK_WDT:
      return "TASK_WDT";
    case ESP_RST_WDT:
      return "WDT";
    case ESP_RST_DEEPSLEEP:
      return "DEEPSLEEP";
    case ESP_RST_BROWNOUT:
      return "BROWNOUT";
    case ESP_RST_SDIO:
      return "SDIO";
    case ESP_RST_CPU_LOCKUP:
      return "CPU_LOCKUP";
    default:
      return "UNKNOWN";
  }
}

bool abnormal(const esp_reset_reason_t r) {
  return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
         r == ESP_RST_BROWNOUT || r == ESP_RST_CPU_LOCKUP;
}

void noteRunning() {
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (!run) {
    diaglog::note("  running=unknown");
    return;
  }
  diaglog::note("  running=%s subtype=0x%02X addr=0x%06X size=0x%06X", run->label, run->subtype,
                static_cast<unsigned>(run->address), static_cast<unsigned>(run->size));
}

// Every partition the bootloader can see: a locked device's table is not
// always the one Lector ships, and "no next-update partition" is answered here.
void notePartitions() {
  esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_ANY, ESP_PARTITION_SUBTYPE_ANY, nullptr);
  int apps = 0;
  while (it != nullptr) {
    const esp_partition_t* p = esp_partition_get(it);
    if (p->type == ESP_PARTITION_TYPE_APP) apps++;
    diaglog::note("  partition %-8s type=%u subtype=0x%02X addr=0x%06X size=0x%06X", p->label, p->type, p->subtype,
                  static_cast<unsigned>(p->address), static_cast<unsigned>(p->size));
    it = esp_partition_next(it);
  }
  esp_partition_iterator_release(it);
  diaglog::note("  app partitions=%d", apps);
}

// The two 32-byte otadata records, raw. The bootloader picks a slot from these
// alone, so a device that keeps booting the old firmware after a "complete"
// install is explained here or in the table above.
void noteOtadata(const char* when) {
  const esp_partition_t* otadata =
      esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_OTA, nullptr);
  if (!otadata) {
    diaglog::note("  otadata %s: NOT FOUND", when);
    return;
  }
  for (int i = 0; i < 2; ++i) {
    ota_boot::SelectEntry e = {};
    const size_t off = static_cast<size_t>(i) * SPI_FLASH_SEC_SIZE;
    if (off + sizeof(e) > otadata->size || esp_partition_read(otadata, off, &e, sizeof(e)) != ESP_OK) {
      diaglog::note("  otadata %s slot%d: unreadable", when, i);
      continue;
    }
    const bool crcOk = e.ota_seq != 0xFFFFFFFFu && e.crc == ota_boot::computeSeqCrc(e.ota_seq);
    diaglog::note("  otadata %s slot%d: seq=%u state=0x%08X crc_ok=%d", when, i, static_cast<unsigned>(e.ota_seq),
                  static_cast<unsigned>(e.ota_state), crcOk ? 1 : 0);
  }
}

void begin(const char* kind, const char* fields) {
  diaglog::beginEntry(kind, fields, now());
  diaglog::note("  firmware=%s board=%s panel=%s reset=%s", CROSSPOINT_VERSION, boardName(), panelName(),
                resetName(esp_reset_reason()));
}

bool writeAll(HalFile& file, const char* data, const size_t len) {
  return len == 0 || file.write(reinterpret_cast<const uint8_t*>(data), len) == len;
}
}  // namespace

void note(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  diaglog::vnote(fmt, args);
  va_end(args);
}

// The two numbers most update failures come down to. A 4 MB write on a low
// cell browns out the SPI rail and shows up as a readback mismatch; a TLS
// handshake with no 8 KB block shows up as a hang.
void noteVitals() {
  const uint16_t mv = powerManager.getBatteryMillivolts();
  if (mv == 0) {
    diaglog::note("  battery=unknown  heap free=%u largest=%u", static_cast<unsigned>(ESP.getFreeHeap()),
                  static_cast<unsigned>(ESP.getMaxAllocHeap()));
    return;
  }
  diaglog::note("  battery=%u%% %umV  heap free=%u largest=%u", powerManager.getBatteryPercentage(), mv,
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

void beginAttempt(const Source source, const char* imagePath, const size_t imageSize) {
  char fields[40];
  snprintf(fields, sizeof(fields), "session=%04x attempt=%u", session(), static_cast<unsigned>(++g_attempt));
  begin("flash attempt", fields);
  char label[48];
  diaglog::imageLabel(imagePath, label, sizeof(label));
  if (imageSize == kUnknownSize) {
    diaglog::note("  source=%s image=%s size=unknown", source == Source::Sd ? "sd" : "ota", label);
  } else {
    diaglog::note("  source=%s image=%s size=%u", source == Source::Sd ? "sd" : "ota", label,
                  static_cast<unsigned>(imageSize));
  }
  noteVitals();
  noteRunning();
  notePartitions();
  noteOtadata("before");
  diaglog::markPending();
}

void recordSwitch(const uint32_t destAddress, const char* destLabel, const uint8_t destSubtype, const bool switchOk) {
  const uint32_t otaIdx = static_cast<uint32_t>(destSubtype) - static_cast<uint32_t>(ESP_PARTITION_SUBTYPE_APP_OTA_0);
  diaglog::note("  switch: dest=%s subtype=0x%02X addr=0x%06X ota_idx=%u ok=%d", destLabel ? destLabel : "?",
                destSubtype, static_cast<unsigned>(destAddress), static_cast<unsigned>(otaIdx), switchOk ? 1 : 0);
  noteOtadata("after");
}

void endAttempt(const char* result, const char* stage) {
  if (stage) {
    diaglog::note("  result=%s stage=%s", result, stage);
  } else {
    diaglog::note("  result=%s", result);
  }
  flush();
}

void recordBootAfterInstall(const uint32_t intendedAddress, const uint32_t runningAddress, const uint32_t imageSize) {
  begin("boot after install", nullptr);
  diaglog::note("  intended=0x%06X running=0x%06X image_size=%u took_effect=%s", static_cast<unsigned>(intendedAddress),
                static_cast<unsigned>(runningAddress), static_cast<unsigned>(imageSize),
                intendedAddress == runningAddress ? "yes" : "NO (bootloader refused the image)");
  noteVitals();
  noteRunning();
  noteOtadata("now");
  diaglog::markPending();
}

void recordAbnormalBoot() {
  const esp_reset_reason_t rst = esp_reset_reason();
  if (!abnormal(rst)) return;
  begin("abnormal boot", nullptr);
  noteVitals();
  noteRunning();
  if (HalSystem::isRebootFromPanic()) {
    // The panic string is an assert or exception name, never user content.
    // The full stack dump is in /crash_report.txt; this line ties the two.
    diaglog::note("  panic=%.120s (full report in /crash_report.txt)", HalSystem::getPanicInfo(false).c_str());
  }
  diaglog::markPending();
}

void recordSdMountFailure() {
  begin("sd fault", nullptr);
  diaglog::note("  card did not mount at boot");
  noteVitals();
  diaglog::markPending();
}

void recordOtaFailure(const char* step, const char* error, const char* screenLine) {
  char fields[40];
  snprintf(fields, sizeof(fields), "session=%04x step=%s", session(), step);
  begin("ota failure", fields);
  diaglog::note("  error=%s screen=\"%s\"", error, screenLine && *screenLine ? screenLine : "");
  noteVitals();
  diaglog::markPending();
  flush();
}

void recordTlsGate(const char* step, const uint32_t freeHeap, const uint32_t largestBlock, const bool framebufferLent,
                   const uint32_t floorFree, const uint32_t floorBlock, const bool allowed, const uint32_t poolFree,
                   const uint32_t poolBlock) {
  char fields[40];
  snprintf(fields, sizeof(fields), "session=%04x step=%s", session(), step);
  diaglog::beginEntry("tls heap gate", fields, now());
  diaglog::note("  free=%u largest=%u framebuffer_lent=%s floor=%u/%u -> %s", static_cast<unsigned>(freeHeap),
                static_cast<unsigned>(largestBlock), framebufferLent ? "yes" : "no", static_cast<unsigned>(floorFree),
                static_cast<unsigned>(floorBlock), allowed ? "allowed" : "REFUSED");
  diaglog::note("  scratch free=%u largest=%u", static_cast<unsigned>(poolFree), static_cast<unsigned>(poolBlock));
}

void recordHeapReclaim(const char* step, const uint32_t before, const uint32_t after, const size_t capacity,
                       const size_t elementSize) {
  char fields[64];
  snprintf(fields, sizeof(fields), "session=%04x step=%s", session(), step);
  begin("heap reclaim", fields);
  diaglog::note("  free_before=%u free_after=%u delta=%d", static_cast<unsigned>(before), static_cast<unsigned>(after),
                static_cast<int>(after) - static_cast<int>(before));
  diaglog::note("  capacity=%u element=%u vector_bytes=%u", static_cast<unsigned>(capacity),
                static_cast<unsigned>(elementSize), static_cast<unsigned>(capacity * elementSize));
  diaglog::markPending();
}

void beginWifiCheckpoint() {
  char fields[32];
  snprintf(fields, sizeof(fields), "session=%04x", session());
  diaglog::beginEntry("wifi connect", fields, now());
  diaglog::markPending();
}

void flush() {
  if (diaglog::size() == 0) return;
  if (!Storage.ready()) {
    LOG_ERR(kTag, "no card; %u bytes held", static_cast<unsigned>(diaglog::size()));
    return;
  }
  const diaglog::Clock at = now();

  // The old file, tail first: when it is over the cap (a 0.31.x file that grew
  // a boot record per wake) the newest entries are the ones worth keeping.
  // ponytail: rewritten in place, not temp+rename; a power cut mid-write loses
  // the file. Add the rename if that is ever reported.
  auto old = makeUniqueNoThrow<char[]>(diaglog::kFileCapBytes);
  size_t oldLen = 0;
  if (old) {
    HalFile in = Storage.open(kFilePath, O_RDONLY);
    if (in) {
      const size_t total = in.fileSize();
      if (total > diaglog::kFileCapBytes) in.seekSet(total - diaglog::kFileCapBytes);
      const int got = in.read(old.get(), diaglog::kFileCapBytes);
      oldLen = got > 0 ? static_cast<size_t>(got) : 0;
    }
    // Reserve both file header lines and the optional dropped-lines notice.
    const size_t room = diaglog::kFileCapBytes - diaglog::kHeaderReserveBytes - diaglog::size();
    oldLen = diaglog::retain(old.get(), oldLen, at.utc, room);
  } else {
    // Keep the newest evidence even under heap pressure; never append without a cap.
    LOG_ERR(kTag, "OOM: %u bytes; retaining current buffer only", static_cast<unsigned>(diaglog::kFileCapBytes));
  }

  HalFile out = Storage.open(kFilePath, O_WRONLY | O_CREAT | O_TRUNC);
  if (!out) {
    LOG_ERR(kTag, "cannot open %s", kFilePath);
    return;
  }
  bool ok = true;
  {
    char when[24];
    diaglog::formatUtc(at.utc, when, sizeof(when));
    char line[160];
    int n = snprintf(line, sizeof(line), "# lector diagnostics v2  firmware=%s board=%s panel=%s  written=%s up=%us\n",
                     CROSSPOINT_VERSION, boardName(), panelName(), when, static_cast<unsigned>(at.uptimeSeconds));
    ok =
        n > 0 && writeAll(out, line, static_cast<size_t>(n) < sizeof(line) ? static_cast<size_t>(n) : sizeof(line) - 1);
    n = snprintf(line, sizeof(line),
                 "# newest entry last; send this whole file. Nothing here names a book, network or device.\n");
    ok = ok && n > 0 && writeAll(out, line, static_cast<size_t>(n));
    if (old)
      ok = ok && writeAll(out, old.get(), oldLen);
    else
      diaglog::note("  retention: low heap, earlier file entries discarded");
  }
  if (diaglog::droppedLines() > 0) {
    char lost[48];
    const int n = snprintf(lost, sizeof(lost), "  (%u older lines lost to the RAM buffer)\n",
                           static_cast<unsigned>(diaglog::droppedLines()));
    ok = ok && n > 0 && writeAll(out, lost, static_cast<size_t>(n));
  }
  ok = ok && writeAll(out, diaglog::data(), diaglog::size());
  out.close();
  if (!ok) {
    LOG_ERR(kTag, "short write to %s", kFilePath);
    return;
  }
  diaglog::clear();
}

void flushIfPending() {
  if (diaglog::pending()) flush();
}

}  // namespace diag
