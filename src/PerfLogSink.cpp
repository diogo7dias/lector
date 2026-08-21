#include "PerfLogSink.h"

#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <PerfLog.h>
#include <PerfStats.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "WakeTiming.h"
#include "platform/LectorX3Pll.h"

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

void startPerfLogSink(const char* device) {
  // The setting is the switch. Off means no directory is created, no file is opened, and
  // PerfLog stays inert for the whole session.
  if (!SETTINGS.showTimings) return;
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
  snprintf(header, sizeof(header), "# device=%s version=%s battery=%u%% pll=0x%02X\n", device, CROSSPOINT_VERSION,
           static_cast<unsigned>(powerManager.getBatteryPercentage()), static_cast<unsigned>(lectorX3PllByte()));
  writeLine(header);
  snprintf(header, sizeof(header),
           "# orientation=%u font=%u size=%upt sleepQuality=%u straightToBook=%u refreshFreq=%u\n",
           static_cast<unsigned>(SETTINGS.orientation), static_cast<unsigned>(SETTINGS.fontFamily),
           static_cast<unsigned>(SETTINGS.fontPointSize), static_cast<unsigned>(SETTINGS.sleepImageQuality),
           static_cast<unsigned>(SETTINGS.wakeStraightToBook), static_cast<unsigned>(SETTINGS.refreshFrequency));
  writeLine(header);
  // The book only affects what is drawn, not what a refresh costs — a refresh drives the
  // whole panel whatever is on it. Recorded so a surprising run can be traced back, not
  // because the two devices need the same book.
  snprintf(header, sizeof(header), "# book=%s\n", APP_STATE.openEpubPath.c_str());
  writeLine(header);

  PerfLog::begin(&writeLine, &commit);
}

void logWakeTimingToPerfLog() {
  if (!PerfLog::isActive()) return;
  char timings[128];
  WakeTiming::formatDiagnostic(timings, sizeof(timings));
  if (timings[0] == '\0') return;
  char line[160];
  snprintf(line, sizeof(line), "wake %s", timings);
  PerfLog::note(line);
}

void logPerfSummary() {
  if (!PerfLog::isActive()) return;
  char lines[PerfStats::kModeCount][64];
  const size_t used = PerfStats::formatSummary(lines, PerfStats::kModeCount);
  for (size_t i = 0; i < used; i++) PerfLog::note(lines[i]);
  char promotedLine[64];
  snprintf(promotedLine, sizeof(promotedLine), "promoted %lu", static_cast<unsigned long>(PerfStats::promotedCount()));
  PerfLog::note(promotedLine);

  // Where the session's panel time actually went. The per-row split answers this for one
  // refresh; this answers it for the run, which is the number that decides whether the
  // next change should be to the waveform, to the bus, or to how much work a frame costs.
  uint64_t wireUs = 0;
  uint64_t waveUs = 0;
  uint64_t totalUs = 0;
  PerfStats::splitTotals(wireUs, waveUs, totalUs);
  if (totalUs > 0) {
    const uint64_t hostUs = totalUs > wireUs + waveUs ? totalUs - wireUs - waveUs : 0;
    char splitLine[96];
    snprintf(splitLine, sizeof(splitLine), "split wire %lu ms wave %lu ms host %lu ms of %lu ms",
             static_cast<unsigned long>(wireUs / 1000), static_cast<unsigned long>(waveUs / 1000),
             static_cast<unsigned long>(hostUs / 1000), static_cast<unsigned long>(totalUs / 1000));
    PerfLog::note(splitLine);
  }

  // How many repaints the session actually painted against how many were asked for. The
  // two being equal means no burst ever collapsed, i.e. holding a button really does cost
  // one refresh per press and the input path needs coalescing of its own.
  char coalesceLine[80];
  snprintf(coalesceLine, sizeof(coalesceLine), "renders %lu of %lu update requests",
           static_cast<unsigned long>(PerfStats::renderPassCount()),
           static_cast<unsigned long>(PerfStats::updateRequestCount()));
  PerfLog::note(coalesceLine);

  // Should read 0. Anything else is a refresh that was handed back while the panel was
  // still driving it -- see EpdBus::waitRefreshComplete. Written unconditionally so a
  // clean run says so explicitly rather than by the line being absent.
  char missedLine[64];
  snprintf(missedLine, sizeof(missedLine), "missed busy assertions %lu",
           static_cast<unsigned long>(EInkDisplay::missedBusyAssertions()));
  PerfLog::note(missedLine);
}
