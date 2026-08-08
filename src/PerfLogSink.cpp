#include "PerfLogSink.h"

#if defined(PERF_LOG) && PERF_LOG

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <PerfLog.h>

#include <cstdio>
#include <cstdlib>

#include "CrossPointSettings.h"
#include "CrossPointState.h"

namespace {

constexpr const char* kDir = "/perf";
// One file per boot. Deep sleep is a chip reset, so a lock-and-wake run would otherwise
// reopen the same path and truncate everything recorded before it: SDCardManager opens
// for write with O_TRUNC and there is no append mode to reach for. A fresh numbered file
// per session keeps every run, and makes the lock/unlock sequence readable as separate
// files rather than one stream with invisible seams.
constexpr int kMaxSessions = 200;

// Held open for the whole session, for the same reason: reopening would truncate. It also
// costs less than an open/close pair per batch, which matters because a batch is written
// in the middle of reading.
HalFile logFile;

bool writeLine(const char* line) {
  if (!logFile.isOpen()) return false;
  logFile.print(line);
  return true;
}

void commit() {
  if (logFile.isOpen()) logFile.flush();
}

}  // namespace

// Held from the boot-time read so the header can report which value this session ran, and
// so repeated calls during init cannot walk the list twice.
uint8_t activePll = 0x09;

unsigned char lectorX3PllByte() {
  static bool chosen = false;
  if (chosen) return activePll;
  chosen = true;

  // Second sweep, walking DOWN. The first one (lector.exp.11) walked up from the stock
  // 0x09 on the sibling datasheet's hint that raising M raises the frame rate. It does
  // not on this panel: two of those five boots measured page turns at ~3138 ms against
  // the usual ~617 ms, so raising the byte made the panel about five times SLOWER. The
  // register plainly works and reaches the hardware; the direction was simply inverted.
  //
  // So the candidates now go below 0x09 rather than above it. 0x09 stays first as the
  // baseline every run is compared against. If none of the three beat it, 0x09 is at or
  // near the panel's ceiling and the frame-clock idea is finished.
  static constexpr uint8_t kCandidates[] = {0x09, 0x08, 0x01, 0x00};
  static constexpr uint8_t kCandidateCount = sizeof(kCandidates) / sizeof(kCandidates[0]);
  static constexpr const char* kIndexPath = "/perf/pll-next.txt";

  uint8_t index = 0;
  char buf[8] = {0};
  if (Storage.readFileToBuffer(kIndexPath, buf, sizeof(buf)) > 0) {
    const int parsed = atoi(buf);
    if (parsed > 0 && parsed < kCandidateCount) index = static_cast<uint8_t>(parsed);
  }
  activePll = kCandidates[index];

  // Advance for the next boot, wrapping so the sweep can simply be repeated. Written
  // before the panel is touched, so a session that crashes mid-sweep still moves on
  // rather than pinning the device to one candidate forever.
  if (Storage.ensureDirectoryExists("/perf")) {
    const uint8_t next = static_cast<uint8_t>((index + 1) % kCandidateCount);
    char out[8] = {0};
    snprintf(out, sizeof(out), "%u\n", static_cast<unsigned>(next));
    Storage.writeFile(kIndexPath, String(out));
  }
  return activePll;
}

void startPerfLogSink(const char* device) {
  if (device == nullptr) device = "dev";
  if (!Storage.ensureDirectoryExists(kDir)) return;

  char filePath[32] = {0};
  for (int session = 0; session < kMaxSessions; session++) {
    snprintf(filePath, sizeof(filePath), "%s/%s-%03d.csv", kDir, device, session);
    if (!Storage.exists(filePath)) break;
  }

  if (!Storage.openFileForWrite("PERF", filePath, logFile)) return;

  // Everything a run needs to be interpreted later, written by the device itself. The
  // alternative was a sheet filled in by hand, which is transcription work and gets
  // things wrong. Panel temperature is deliberately absent: neither driver can read one
  // back — the temperature values in the driver are constants written TO the panel.
  char header[192];
  // lectorX3PllByte(), NOT activePll. This sink starts right after the card mounts, which
  // is BEFORE setupDisplayAndFonts() constructs the panel driver, so at this point nothing
  // has chosen a candidate yet and activePll still holds its initialiser. Reading it here
  // stamped "pll=0x09" on all five lector.exp.11 runs while the panel was in fact running
  // different values — the sweep worked and the log denied it. Calling the chooser instead
  // makes the choice here, where the card is up, and the driver later gets the cached value.
  snprintf(header, sizeof(header), "# device=%s version=%s battery=%u%% pll=0x%02X\n", device, CROSSPOINT_VERSION,
           static_cast<unsigned>(powerManager.getBatteryPercentage()), static_cast<unsigned>(lectorX3PllByte()));
  writeLine(header);
  snprintf(header, sizeof(header), "# orientation=%u font=%u size=%upt sleepQuality=%u straightToBook=%u\n",
           static_cast<unsigned>(SETTINGS.orientation), static_cast<unsigned>(SETTINGS.fontFamily),
           static_cast<unsigned>(SETTINGS.fontPointSize), static_cast<unsigned>(SETTINGS.sleepImageQuality),
           static_cast<unsigned>(SETTINGS.wakeStraightToBook));
  writeLine(header);
  // The book only affects what is drawn, not what a refresh costs — a refresh drives the
  // whole panel whatever is on it. Recorded so a surprising run can be traced back, not
  // because the two devices need the same book.
  snprintf(header, sizeof(header), "# book=%s\n", APP_STATE.openEpubPath.c_str());
  writeLine(header);

  PerfLog::begin(&writeLine, &commit);
}

#else

void startPerfLogSink(const char*) {}

// Stock frame clock: the stable firmware never sweeps.
unsigned char lectorX3PllByte() { return 0x09; }

#endif  // PERF_LOG
