#include "WakeTiming.h"

#include <Arduino.h>
#include <HalStorage.h>

#include <cstdio>
#include <cstring>

#include "Logging.h"

namespace WakeTiming {
namespace {

// Where the stamps actually live.
//
// RTC_NOINIT was the first home, and on this hardware it does not survive the sleep: the
// X3 reported "w0 rtc lost" on every wake, with the magic word coming back as decaying
// noise (fb89e5c7, fb89e1c7, fb81e1c7 across three wakes) rather than the value written.
// That is a power domain going down, not a bug in the stamping, so no amount of care in
// RAM could have fixed it. The card survives a full power cut, which is exactly the
// property this needs. Thirty-six bytes, written once per wake.
constexpr const char* kDir = "/.crosspoint";
constexpr const char* kPath = "/.crosspoint/waketiming.bin";

constexpr uint8_t kCount = static_cast<uint8_t>(Stage::Count);

// A stage that never ran. Distinct from 0, which is a legitimate stamp very early in boot.
constexpr uint16_t kUnset = 0xFFFF;

// Guards the record: a short read or a file from an older format must not print as
// timings. "WAK3" — the record gained three stages when the banner stage was split, so a
// WAK2 file has fewer stamps than this build expects and must be rejected, not
// misread as a fast wake. "WAK4" for the same reason: the pre-SD stage was split three
// ways, so a WAK3 record is shorter again.
constexpr uint32_t kMagic = 0x57414B34;

// magic (4) + one stamp per stage + wake count (4). Sized off kCount so adding a stage
// cannot leave the reader and the writer disagreeing. Fixed and written whole, so a
// partial write from a card pulled mid-flush fails the size check on the next read.
constexpr size_t kRecordBytes = 4 + kCount * sizeof(uint16_t) + 4;

// Plain RAM now. Nothing here has to outlive the boot: the card carries the numbers
// across the sleep, and everything below is rebuilt from the file each wake.
uint16_t current[kCount];
uint16_t previous[kCount];
uint32_t wakeCount = 0;

// Whether loadPrevious() ran, and whether it found a good record. The two are different
// answers: "the card was never read" is a wiring mistake in the boot order, while "read,
// found nothing" is a normal first run after a flash. The banner must not conflate them.
bool loadAttempted = false;
bool loadedOk = false;
bool enabled = false;

// Short labels, in Stage order. "pre" is the prologue before the first stamp, which is
// printed as a stage in its own right rather than being silently rolled into the total.
constexpr const char* kStageNames[kCount] = {"pre",  "gpio",  "hal",  "sd",   "cfg",  "in",
                                             "disp", "frame", "base", "draw", "push", "act"};

// millis() is 32-bit; a wake that reaches 65 seconds is already broken, and clamping
// keeps the display honest rather than wrapping to a small, believable-looking number.
uint16_t clampMs(const unsigned long ms) { return ms >= kUnset ? (kUnset - 1) : static_cast<uint16_t>(ms); }

}  // namespace

void beginWake() {
  // The previous wake is no longer inherited from memory — loadPrevious() reads it off
  // the card once Storage is up. This only clears the slate for the wake starting now.
  for (uint8_t i = 0; i < kCount; i++) {
    current[i] = kUnset;
    previous[i] = kUnset;
  }
  wakeCount = 0;
  loadAttempted = false;
  loadedOk = false;
}

void mark(const Stage stage) {
  const uint8_t i = static_cast<uint8_t>(stage);
  if (i >= kCount) return;
  current[i] = clampMs(millis());
}

void formatPrevious(char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  if (!loadedOk || previous[0] == kUnset) return;

  // The first stamp is printed as a stage of its own rather than used only as the
  // baseline. Everything before it is real wake time — the framework's startup and
  // Serial — and leaving it out of the line made it findable only by subtracting the
  // printed stages from the total, which nobody does.
  size_t used = 0;
  uint16_t last = previous[0];
  {
    const int n = snprintf(out, outLen, "%s %u", kStageNames[0], static_cast<unsigned>(previous[0]));
    if (n <= 0 || static_cast<size_t>(n) >= outLen) return;
    used = static_cast<size_t>(n);
  }
  for (uint8_t i = 1; i < kCount; i++) {
    if (previous[i] == kUnset) continue;
    // Stamps are monotonic in practice; guard anyway so a missed stage cannot underflow.
    const uint16_t delta = previous[i] > last ? static_cast<uint16_t>(previous[i] - last) : 0;
    const int n = snprintf(out + used, outLen - used, "%s%s %u", used == 0 ? "" : " ", kStageNames[i], delta);
    if (n <= 0 || static_cast<size_t>(n) >= outLen - used) return;
    used += static_cast<size_t>(n);
    last = previous[i];
  }
  if (used == 0) return;
  snprintf(out + used, outLen - used, " = %u", static_cast<unsigned>(last));
}

void setEnabled(const bool value) { enabled = value; }

// The card work exists to feed the timings readouts, so it is switched with them. A
// device that is not being measured must not pay an SD write on every single wake.
void loadPrevious() {
  if (!enabled) return;
  // Called after the card is up (Stage::SdReady), not from beginWake(): at beginWake()
  // time Storage has not been started yet, and the banner that reads these numbers is
  // painted well after the card is mounted.
  loadAttempted = true;

  HalFile file;
  if (!Storage.openFileForRead("WAKET", kPath, file)) return;  // first run after a flash

  uint8_t buf[kRecordBytes];
  if (file.read(buf, sizeof(buf)) != static_cast<int>(sizeof(buf))) return;

  uint32_t fileMagic = 0;
  memcpy(&fileMagic, buf, sizeof(fileMagic));  // memcpy, not a cast: RISC-V faults unaligned
  if (fileMagic != kMagic) return;

  memcpy(previous, buf + 4, sizeof(previous));
  memcpy(&wakeCount, buf + 4 + sizeof(previous), sizeof(wakeCount));
  loadedOk = true;
}

void persist() {
  if (!enabled) return;
  // Written at the end of the wake, once every stage has been stamped. The count is the
  // one loaded plus this wake, so a climbing number proves the file is being read back.
  uint8_t buf[kRecordBytes];
  const uint32_t magicOut = kMagic;
  const uint32_t countOut = wakeCount + 1;
  memcpy(buf, &magicOut, sizeof(magicOut));
  memcpy(buf + 4, current, sizeof(current));
  memcpy(buf + 4 + sizeof(current), &countOut, sizeof(countOut));

  Storage.ensureDirectoryExists(kDir);
  HalFile file;
  if (!Storage.openFileForWrite("WAKET", kPath, file)) {
    LOG_ERR("WAKET", "cannot write %s", kPath);
    return;
  }
  file.write(buf, sizeof(buf));
  file.flush();
}

void formatDiagnostic(char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';

  // The banner was drawn before the card was read. That is a boot-order fault in this
  // firmware, not a fault on the device, and it must not masquerade as "no data yet".
  if (!loadAttempted) {
    snprintf(out, outLen, "wake: not read yet");
    return;
  }

  // Read, found nothing usable: no file, a short file, or a foreign magic. Normal on the
  // first wake after a flash, because no wake has been written yet.
  if (!loadedOk) {
    snprintf(out, outLen, "wake: no file yet");
    return;
  }

  char timings[160];
  formatPrevious(timings, sizeof(timings));
  if (timings[0] == '\0') {
    snprintf(out, outLen, "w%lu no stamps", static_cast<unsigned long>(wakeCount));
    return;
  }
  snprintf(out, outLen, "w%lu %s", static_cast<unsigned long>(wakeCount), timings);
}

}  // namespace WakeTiming
