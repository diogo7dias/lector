#include "PerfLogSink.h"

#if defined(PERF_LOG) && PERF_LOG

#include <HalPowerManager.h>
#include <HalStorage.h>
#include <PerfLog.h>

#include <cstdio>

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
  // Commit periodically rather than per line. A batch is 128 lines, so this costs four
  // commits per batch and bounds what an unclean power-off can lose to 32 refreshes.
  static uint32_t linesSinceCommit = 0;
  if (++linesSinceCommit >= 32) {
    linesSinceCommit = 0;
    logFile.flush();
  }
  return true;
}

}  // namespace

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
  snprintf(header, sizeof(header), "# device=%s version=%s battery=%u%%\n", device, CROSSPOINT_VERSION,
           static_cast<unsigned>(powerManager.getBatteryPercentage()));
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

  PerfLog::begin(&writeLine);
  // Flushed rather than closed after each batch: the handle stays open for the session,
  // so an unclean power-off loses at most the batch not yet written.
  logFile.flush();
}

#else

void startPerfLogSink(const char*) {}

#endif  // PERF_LOG
