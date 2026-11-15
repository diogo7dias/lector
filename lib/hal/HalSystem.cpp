#include "HalSystem.h"

#include <string>

#include "Arduino.h"
#include "HalStorage.h"
#include "Logging.h"
#include "SymbolTable.h"
#include "esp_debug_helpers.h"
#include "esp_private/esp_cpu_internal.h"
#include "esp_private/esp_system_attr.h"
#include "esp_private/panic_internal.h"

#if !__riscv
#include "esp_cpu_utils.h"
#include "xtensa_context.h"
#endif

#define MAX_PANIC_STACK_DEPTH 32
#define PANIC_CAPTURE_MAGIC 0x50414E49u

RTC_NOINIT_ATTR char panicMessage[256];
RTC_NOINIT_ATTR HalSystem::StackFrame panicStack[MAX_PANIC_STACK_DEPTH];
// PC and the return-address register (ra, or a0 on Xtensa) at the fault; 0 when not captured.
RTC_NOINIT_ATTR uint32_t panicPc;
RTC_NOINIT_ATTR uint32_t panicReturnAddress;
// RTC_NOINIT is uninitialized on cold boot, so only this exact marker proves a
// panic diagnostic was captured before the reset.
RTC_NOINIT_ATTR volatile uint32_t panicCaptureMarker;

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
  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_abort(message);
}

void IRAM_ATTR __wrap_panic_print_backtrace(const void* frame, int core) {
  if (!frame) {
    __real_panic_print_backtrace(frame, core);
    return;
  }

  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }

#if !__riscv
  // Xtensa (X4 Pro): the windowed ABI lets IDF walk real frames, so each row holds the
  // caller PCs (8 per row, row sp = first frame's sp), ready for xtensa addr2line.
  const XtExcFrame* xt = static_cast<const XtExcFrame*>(frame);
  panicPc = static_cast<uint32_t>(xt->pc);
  panicReturnAddress = static_cast<uint32_t>(xt->a0);
  esp_backtrace_frame_t bt = {static_cast<uint32_t>(xt->pc), static_cast<uint32_t>(xt->a1),
                              static_cast<uint32_t>(xt->a0), xt};
  int row = 0;
  int slot = 0;
  panicStack[0].sp = bt.sp;
  for (int y = 0; y < 8; y++) panicStack[0].spp[y] = 0;
  while (true) {
    panicStack[row].spp[slot] = esp_cpu_process_stack_pc(bt.pc);
    if (!bt.next_pc || !esp_backtrace_get_next_frame(&bt)) break;
    if (++slot == 8) {
      slot = 0;
      if (++row >= MAX_PANIC_STACK_DEPTH) break;
      panicStack[row].sp = bt.sp;
      for (int y = 0; y < 8; y++) panicStack[row].spp[y] = 0;
    }
  }

  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_print_backtrace(frame, core);
#else

  panicPc = (uint32_t)((RvExcFrame*)frame)->mepc;
  panicReturnAddress = (uint32_t)((RvExcFrame*)frame)->ra;

  // Copied from components/esp_system/port/arch/riscv/panic_arch.c
  uint32_t sp = (uint32_t)((RvExcFrame*)frame)->sp;
  const int per_line = 8;
  int depth = 0;
  for (int x = 0; x < 1024; x += per_line * sizeof(uint32_t)) {
    uint32_t* spp = (uint32_t*)(sp + x);
    // panic_print_hex(sp + x);
    // panic_print_str(": ");
    panicStack[depth].sp = sp + x;
    for (int y = 0; y < per_line; y++) {
      // panic_print_str("0x");
      // panic_print_hex(spp[y]);
      // panic_print_str(y == per_line - 1 ? "\r\n" : " ");
      panicStack[depth].spp[y] = spp[y];
    }

    depth++;
    if (depth >= MAX_PANIC_STACK_DEPTH) {
      break;
    }
  }

  panicCaptureMarker = PANIC_CAPTURE_MAGIC;

  __real_panic_print_backtrace(frame, core);
#endif
}
}

namespace HalSystem {

namespace {
// Read once in begin(): checkPanic() zeroes the capture marker after a dump, so a live
// read would change its answer partway through boot.
bool rebootedFromPanic = false;

bool detectPanicReboot() {
  const auto resetReason = esp_reset_reason();
  if (resetReason == ESP_RST_PANIC || resetReason == ESP_RST_CPU_LOCKUP) {
    return true;
  }

  // A watchdog reset only counts as a crash when a panic handler actually ran
  // and captured something. A bare timeout reset carries no reason, and
  // reporting it opened the crash screen with an empty message.
  const bool watchdogReset =
      resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_WDT;
  return watchdogReset && panicCaptureMarker == PANIC_CAPTURE_MAGIC;
}
}  // namespace

void begin() {
  rebootedFromPanic = detectPanicReboot();
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
    auto file = Storage.open("/crash_report.txt", O_WRITE | O_CREAT | O_TRUNC);
    if (file) {
      const size_t written = file.write(panicInfo.c_str(), panicInfo.size());
      file.close();
      if (written == panicInfo.size()) {
        // Keep the crash data for CrashActivity, but mark it consumed so a
        // later watchdog reset cannot be mistaken for this panic.
        panicCaptureMarker = 0;
        LOG_INF("SYS", "Dumped panic info to SD card");
      } else {
        LOG_ERR("SYS", "Failed to write complete crash report (%zu of %zu bytes)", written, panicInfo.size());
      }
    } else {
      LOG_ERR("SYS", "Failed to open crash_report.txt for writing");
    }
  }
}

void clearPanic() {
  panicCaptureMarker = 0;
  panicMessage[0] = '\0';
  panicPc = 0;
  panicReturnAddress = 0;
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH; i++) {
    panicStack[i].sp = 0;
  }
  clearLastLogs();
}

// On Xtensa a saved return address carries the call size in its top two bits.
static uint32_t toCodeAddress(uint32_t value) {
#if __riscv
  return value;
#else
  return (value >> 30) ? ((value & 0x3FFFFFFF) | 0x40000000) : value;
#endif
}

// Lists the faulting PC and return address, then the stack words that fall
// inside a function: candidates for return addresses, not a real unwind.
// On Xtensa the rows hold the unwound caller PCs instead of raw stack words.
static void appendCodeAddresses(std::string& info) {
  SymbolTable::Table table;
  const bool haveTable = SymbolTable::open(table);

  char line[160];
  auto append = [&](const char* label, uint32_t address, bool isReturnAddress, bool onlyIfNamed) {
    // A call that never returns can be a function's last instruction, so its return
    // address is the next function's first byte. Resolve the byte before it instead.
    const uint32_t back = isReturnAddress ? 1 : 0;
    SymbolTable::Function function;
    if (haveTable && address >= back && SymbolTable::lookup(table, address - back, function)) {
      const uint32_t offset = function.offset + back;
      if (function.isLibraryRange) {
        // The offset in a whole library means nothing.
        snprintf(line, sizeof(line), "\n%s0x%08X %s", label, static_cast<unsigned>(address), function.name);
      } else {
        snprintf(line, sizeof(line), "\n%s0x%08X %s+0x%x%s", label, static_cast<unsigned>(address), function.name,
                 static_cast<unsigned>(offset), offset == function.size ? " (function end)" : "");
      }
    } else if (onlyIfNamed) {
      return false;
    } else {
      snprintf(line, sizeof(line), "\n%s0x%08X ?", label, static_cast<unsigned>(address));
    }
    info += line;
    return true;
  };

  info += "\n\nCode addresses:";
  append("PC ", panicPc, false, false);
  append("RA ", toCodeAddress(panicReturnAddress), true, false);
  if (!haveTable) {
    return;
  }
  info += "\nStack (possible return addresses):";
  // Bounds the report: each line is a heap append on a device with little RAM.
  int remaining = 64;
  for (size_t i = 0; i < MAX_PANIC_STACK_DEPTH && panicStack[i].sp != 0; i++) {
    for (size_t j = 0; j < 8 && remaining > 0; j++) {
      if (append("  ", toCodeAddress(panicStack[i].spp[j]), true, true)) {
        remaining--;
      }
    }
  }
}

std::string getPanicInfo(bool full) {
  if (!full) {
    return panicMessage;
  } else {
    std::string info;

    info += "CrossPoint version: " CROSSPOINT_VERSION;
    info += "\n\nPanic reason: " + std::string(panicMessage);
    appendCodeAddresses(info);
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

bool isRebootFromPanic() { return rebootedFromPanic; }

}  // namespace HalSystem
