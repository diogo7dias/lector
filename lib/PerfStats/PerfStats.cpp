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

uint32_t promoted = 0;
uint32_t renderPasses = 0;
uint32_t updateRequests = 0;

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

}  // namespace

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
                 const uint32_t waveUs) {
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
