#include <PerfStats.h>

#include <cstdio>

namespace PerfStats {
namespace {

// Per-mode totals since boot, for the end-of-session summary.
struct ModeStats {
  uint32_t count = 0;
  uint32_t minUs = 0;
  uint32_t maxUs = 0;
  uint64_t sumUs = 0;
};

ModeStats stats[kModeCount];

// The press waiting for a paint. 0 is a legitimate millis() value in the first
// millisecond of a boot, so "no press outstanding" needs its own flag rather than a
// sentinel timestamp.
uint32_t inputAtMs = 0;
bool inputPending = false;

// Running totals of the split, so the summary can say where a whole session went rather
// than only where the last refresh went.
uint64_t sumWireUs = 0;
uint64_t sumWaveUs = 0;
uint64_t sumTotalUs = 0;

// ponytail: eight submissions cover normal wakes; count exposes overflow and the
// existing per-refresh CSV supplies the rest on unusually slow/indexing wakes.
uint16_t wakePanels[8] = {};
uint32_t wakeCount = 0;
bool wakeFinished = false;

uint32_t promoted = 0;

struct LastRefresh {
  uint8_t requested;
  uint8_t actual;
  uint16_t thinkMs;
  uint32_t totalUs;
  uint32_t wireUs;
  uint32_t waveUs;
};
LastRefresh last{};
bool haveLast = false;
uint32_t renderPasses = 0;
uint32_t updateRequests = 0;

}  // namespace

const char* modeName(const uint8_t mode) {
  switch (mode) {
    case 0:
      return "FULL";
    case 1:
      return "HALF";
    case 2:
      return "FAST";
    default:
      return "?";
  }
}

void noteInput(const uint32_t ms) {
  inputAtMs = ms;
  inputPending = true;
}

uint16_t takeThinkMs(const uint32_t nowMs) {
  if (!inputPending) return kNoThink;
  inputPending = false;
  const uint32_t delta = nowMs - inputAtMs;  // unsigned: a millis() wrap still subtracts right
  // A press that is seconds old did not cause this refresh — a background build or a
  // timed repaint did. Reporting it as think time would blame the firmware for a wait
  // the reader never sat through.
  if (delta >= 5000) return kNoThink;
  return static_cast<uint16_t>(delta);
}

void noteRefresh(const uint8_t requestedMode, const uint8_t actualMode, const uint32_t totalUs, const uint32_t wireUs,
                 const uint32_t waveUs, const uint16_t thinkMs) {
  last = {requestedMode, actualMode, thinkMs, totalUs, wireUs, waveUs};
  haveLast = true;
  if (!wakeFinished) {
    if (wakeCount < 8) {
      const uint32_t ms = totalUs / 1000;
      wakePanels[wakeCount] = ms < 65535 ? ms : 65535;
    }
    ++wakeCount;
  }
  if (actualMode < kModeCount) {
    ModeStats& s = stats[actualMode];
    if (s.count == 0 || totalUs < s.minUs) s.minUs = totalUs;
    if (totalUs > s.maxUs) s.maxUs = totalUs;
    s.count++;
    s.sumUs += totalUs;
  }
  // FAST is mode 2 and the only mode the policy promotes out of; a caller that asked for
  // HALF and got HALF is not a promotion, however slow it was.
  if (requestedMode == 2 && actualMode != 2) promoted++;

  sumWireUs += wireUs;
  sumWaveUs += waveUs;
  sumTotalUs += totalUs;
}

uint32_t wakePanelCount() { return wakeCount; }

void finishWakePanels() { wakeFinished = true; }

void formatWakePanels(char* out, const size_t size) {
  size_t used = 0;
  if (!size) return;
  out[0] = '\0';
  for (uint32_t i = 0; i < wakeCount && i < 8; ++i) {
    const int n = snprintf(out + used, size - used, "%s%u", i ? "/" : "", wakePanels[i]);
    if (n < 0 || static_cast<size_t>(n) >= size - used) return;
    used += n;
  }
}

void splitTotals(uint64_t& wireUs, uint64_t& waveUs, uint64_t& totalUs) {
  wireUs = sumWireUs;
  waveUs = sumWaveUs;
  totalUs = sumTotalUs;
}

size_t formatSummary(char (*const lines)[64], const size_t maxLines) {
  size_t used = 0;
  for (uint8_t mode = 0; mode < kModeCount && used < maxLines; mode++) {
    const ModeStats& s = stats[mode];
    if (s.count == 0) continue;
    const uint32_t avgUs = static_cast<uint32_t>(s.sumUs / s.count);
    snprintf(lines[used], sizeof(lines[used]), "%s n%lu min %lu avg %lu max %lu ms", modeName(mode),
             static_cast<unsigned long>(s.count), static_cast<unsigned long>(s.minUs / 1000),
             static_cast<unsigned long>(avgUs / 1000), static_cast<unsigned long>(s.maxUs / 1000));
    used++;
  }
  return used;
}

void formatLastLine(char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  if (!haveLast) return;
  char mode[12];
  if (last.requested == last.actual) {
    snprintf(mode, sizeof(mode), "%s", modeName(last.actual));
  } else {
    snprintf(mode, sizeof(mode), "%s>%s", modeName(last.requested), modeName(last.actual));
  }
  char think[8] = "-";
  if (last.thinkMs != kNoThink) snprintf(think, sizeof(think), "%u", static_cast<unsigned>(last.thinkMs));
  snprintf(out, outLen, "%s t%s p%lu w%lu v%lu", mode, think, static_cast<unsigned long>(last.totalUs / 1000),
           static_cast<unsigned long>(last.wireUs / 1000), static_cast<unsigned long>(last.waveUs / 1000));
}

bool lastRefresh(uint8_t& actualMode, uint32_t& totalUs, uint32_t& wireUs, uint32_t& waveUs) {
  actualMode = last.actual;
  totalUs = last.totalUs;
  wireUs = last.wireUs;
  waveUs = last.waveUs;
  return haveLast;
}

uint32_t promotedCount() { return promoted; }

void noteRenderPass(const uint32_t requestsServed) {
  renderPasses++;
  // A pass can be woken with a count of 0 only if the notification was already consumed,
  // which cannot happen here; guard anyway so the ratio can never read below 1.
  updateRequests += requestsServed > 0 ? requestsServed : 1;
}

uint32_t renderPassCount() { return renderPasses; }

uint32_t updateRequestCount() { return updateRequests; }

}  // namespace PerfStats
