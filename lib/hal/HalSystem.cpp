#include "HalSystem.h"

#include <inttypes.h>
#include <time.h>

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "esp_cpu.h"
#include "esp_debug_helpers.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"
#include "esp_rom_sys.h"

#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
RTC_NOINIT_ATTR uint32_t panicPc;
RTC_NOINIT_ATTR uint32_t panicLr;
RTC_NOINIT_ATTR uint32_t panicFreeHeap;
// RTC_NOINIT is uninitialized on cold boot, so only this exact marker proves a
// panic diagnostic was captured before the reset.
RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;

static inline uint32_t esp_cpu_get_pc_val() {
  uint32_t pc = 0;
#if defined(__riscv)
  asm volatile("auipc %0, 0" : "=r"(pc));
#else
  pc = (uint32_t)(uintptr_t)__builtin_return_address(0);
#endif
  return pc;
}

static inline uint32_t esp_cpu_get_lr_val() {
  uint32_t lr = 0;
#if defined(__riscv)
  asm volatile("mv %0, ra" : "=r"(lr));
#else
  lr = (uint32_t)(uintptr_t)__builtin_return_address(0);
#endif
  return lr;
}

extern "C" {

void __real_panic_abort(const char* message);
void __real_panic_print_backtrace(const void* frame, int core);

static DRAM_ATTR const char PANIC_REASON_UNKNOWN[] = "(unknown panic reason)";
void IRAM_ATTR __wrap_panic_abort(const char* message) {
  if (!message) message = PANIC_REASON_UNKNOWN;
  // IRAM-safe bounded copy (strncpy is not IRAM-safe in panic context)
  int i = 0;
  for (; i < (int)sizeof(panicMessage) - 1 && message[i]; i++) {
    panicMessage[i] = message[i];
  }
  panicMessage[i] = '\0';
  panicPc = esp_cpu_get_pc_val();
  panicLr = esp_cpu_get_lr_val();
  panicFreeHeap = esp_get_free_heap_size();
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  esp_rom_printf("\r\n--- PANIC ABORT: %s ---\r\n", panicMessage);
  esp_rom_printf("PC: 0x%08" PRIx32 "  LR: 0x%08" PRIx32 "  Free heap: %" PRIu32 " bytes\r\n", panicPc, panicLr,
                 panicFreeHeap);

  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    panicPc = esp_cpu_get_pc_val();
    panicLr = esp_cpu_get_lr_val();
    panicFreeHeap = esp_get_free_heap_size();
    panicCaptureMarker = PANIC_CAPTURE_MAGIC;
    __real_panic_print_backtrace(frame, core);
    return;
  }

#if !__riscv
  panicPc = esp_cpu_get_pc_val();
  panicLr = esp_cpu_get_lr_val();
  panicFreeHeap = esp_get_free_heap_size();
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;
  esp_rom_printf("\r\n--- CRASH DUMP: Core %d ---\r\n", core);
  esp_rom_printf("PC: 0x%08" PRIx32 "  LR: 0x%08" PRIx32 "  Free heap: %" PRIu32 " bytes\r\n", panicPc, panicLr,
                 panicFreeHeap);
  __real_panic_print_backtrace(frame, core);
  return;
#else
  const auto* rvFrame = static_cast<const RvExcFrame*>(frame);
  panicPc = rvFrame->mepc;
  panicLr = rvFrame->ra;
  panicFreeHeap = esp_get_free_heap_size();

  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }

  // Copied from components/esp_system/port/arch/riscv/panic_arch.c
  uint32_t sp = (uint32_t)rvFrame->sp;
  const int per_line = 8;
  int depth = 0;
  for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
    uint32_t* spp = (uint32_t*)(sp + x);
    panicStack[depth].sp = sp + x;
    for (int y = 0; y < per_line; y++) {
      panicStack[depth].spp[y] = spp[y];
    }

    depth++;
    if (depth >= MAX_PANIC_STACK_DEPTH) {
      break;
    }
  }

  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  esp_rom_printf("\r\n--- CRASH DUMP: Core %d ---\r\n", core);
  esp_rom_printf("PC: 0x%08" PRIx32 "  LR: 0x%08" PRIx32 "  Free heap: %" PRIu32 " bytes\r\n", panicPc, panicLr,
                 panicFreeHeap);
  esp_rom_printf("Reason: %s\r\n", panicMessage[0] ? panicMessage : PANIC_REASON_UNKNOWN);

  __real_panic_print_backtrace(frame, core);
#endif
}
}

namespace HalSystem {

static void writeCrashDumpToSd(const std::string& panicInfo) {
  const time_t now = time(nullptr);
  const unsigned long ts = (now > 1577836800) ? static_cast<unsigned long>(now) : millis();
  char crashFileName[48];
  snprintf(crashFileName, sizeof(crashFileName), "/crash-%lu.log", ts);

  auto file = Storage.open(crashFileName, O_WRITE | O_CREAT | O_TRUNC);
  if (file) {
    const size_t written = file.write(panicInfo.c_str(), panicInfo.size());
    file.close();
    if (written == panicInfo.size()) {
      LOG_INF("SYS", "Dumped panic info to SD card: %s", crashFileName);
    } else {
      LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes) to %s", written, panicInfo.size(),
              crashFileName);
    }
  } else {
    LOG_ERR("SYS", "Failed to open %s for writing", crashFileName);
  }

  auto reportFile = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
  if (reportFile) {
    reportFile.write(panicInfo.c_str(), panicInfo.size());
    reportFile.close();
  }
}

void begin() {
  // This is mostly for the first boot, we need to initialize the panic info and logs to empty state
  // If we reboot from a panic state, we want to keep the panic info until we successfully dump it to the SD card, use
  // `clearPanic()` to clear it after dumping
  if (!isRebootFromPanic()) {
    clearPanic();
  } else {
    // Panic reboot: preserve logs and panic info, but clamp logHead in case the
    // panic occurred before begin() ever ran (e.g. in a static constructor).
    // If logHead was out of range, logMessages is also garbage — clear it so
    // getLastLogs() does not dump corrupt data into the crash report.
    if (sanitizeLogHead()) {
      clearLastLogs();
    }
  }
}

void checkPanic() {
  if (isRebootFromPanic()) {
    auto panicInfo = getPanicInfo(true);
    writeCrashDumpToSd(panicInfo);
    panicCaptureMarker = 0;
  }
}

void clearPanic() {
  panicCaptureMarker = 0;
  panicPc = 0;
  panicLr = 0;
  panicFreeHeap = 0;
  panicMessage[0] = '\0';
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += "CrossPoint version: " CROSSPOINT_VERSION "\n";
    char buf[128];
    snprintf(buf, sizeof(buf), "PC: 0x%08" PRIX32 "  LR: 0x%08" PRIX32 "\n", panicPc, panicLr);
    info += buf;
    snprintf(buf, sizeof(buf), "Free heap at crash: %" PRIu32 " bytes\n", panicFreeHeap);
    info += buf;
    info += "\nPanic reason: " + std::string(panicMessage[0] ? panicMessage : "(unknown)");
    info += "\n\nLast logs:\n" + getLastLogs();
    info += "\n\nStack memory:\n";

    auto toHex = [](uint32_t value) {
      char buffer[9];
      snprintf(buffer, sizeof(buffer), "%08X", value);
      return std::string(buffer);
    };
    for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
      if (panicStack[i].sp == 0) {
        break;
      }
      info += "0x" + toHex(panicStack[i].sp) + ": ";
      for (size_t j = 0; j < 8; j++) {
        info += "0x" + toHex(panicStack[i].spp[j]) + " ";
      }
      info += "\n";
    }

    return info;
  }
}

bool isRebootFromPanic() {
  if (panicCaptureMarker == PANIC_CAPTURE_MAGIC) {
    return true;
  }
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP) {
    return true;
  }

  // A watchdog reset only counts as a crash when a panic handler actually ran
  // and captured something. A bare timeout reset carries no reason, and
  // reporting it opened the crash screen with an empty message.
  const bool watchdogReset =
      resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  return watchdogReset;
}

void crashDump(const char* reason) {
  panicPc = esp_cpu_get_pc_val();
  panicLr = esp_cpu_get_lr_val();
  panicFreeHeap = esp_get_free_heap_size();
  if (reason) {
    strncpy(panicMessage, reason, sizeof(panicMessage) - 1);
    panicMessage[sizeof(panicMessage) - 1] = '\0';
  } else {
    strncpy(panicMessage, "(explicit crash dump)", sizeof(panicMessage) - 1);
  }
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  logSerial.printf("\r\n=== CRASH DUMP ===\r\n");
  logSerial.printf("PC: 0x%08" PRIX32 "  LR: 0x%08" PRIX32 "\r\n", panicPc, panicLr);
  logSerial.printf("Free heap: %" PRIu32 " bytes\r\n", panicFreeHeap);
  logSerial.printf("Reason: %s\r\n", panicMessage);
  logSerial.printf("Call stack:\r\n");
  esp_backtrace_print(32);
  logSerial.printf("\r\nLast logs:\r\n%s\r\n", getLastLogs().c_str());
  logFlush();

  if (Storage.ready()) {
    const std::string panicInfo = getPanicInfo(true);
    writeCrashDumpToSd(panicInfo);
  }

  esp_restart();
}

}  // namespace HalSystem
