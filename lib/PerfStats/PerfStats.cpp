#include <PerfStats.h>

#include <cstdio>

namespace PerfStats {
namespace {

ModeStats stats[kModeCount];

// The press waiting for a paint. 0 is a legitimate millis() value in the first
// millisecond of a boot, so "no press outstanding" needs its own flag rather than a
// sentinel timestamp.
uint32_t inputAtMs = 0;
bool inputPending = false;

// The previous refresh, held for the overlay.
uint8_t lastRequested = 0;
uint8_t lastActual = 0;
uint32_t lastTotalUs = 0;
uint32_t lastAsyncUs = 0;
uint16_t lastThinkMs = kNoThink;
uint16_t lastInkScore = 0;
uint16_t lastInkDebt = 0;
bool haveLast = false;

uint32_t promoted = 0;
uint8_t activePll = 0;

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

void noteRefresh(const uint8_t requestedMode, const uint8_t actualMode, const uint32_t totalUs,
                 const uint32_t asyncStartUs, const uint16_t thinkMs, const uint16_t inkScore, const uint16_t inkDebt) {
  if (actualMode < kModeCount) {
    ModeStats& s = stats[actualMode];
    if (s.count == 0 || totalUs < s.minUs) s.minUs = totalUs;
    if (totalUs > s.maxUs) s.maxUs = totalUs;
    s.count++;
    s.lastUs = totalUs;
    s.sumUs += totalUs;
  }
  // FAST is mode 2 and the only mode the policy promotes out of; a caller that asked for
  // HALF and got HALF is not a promotion, however slow it was.
  if (requestedMode == 2 && actualMode != 2) promoted++;

  lastRequested = requestedMode;
  lastActual = actualMode;
  lastTotalUs = totalUs;
  lastAsyncUs = asyncStartUs;
  lastThinkMs = thinkMs;
  lastInkScore = inkScore;
  lastInkDebt = inkDebt;
  haveLast = true;
}

void formatLastLine(char* const out, const size_t outLen) {
  if (outLen == 0) return;
  out[0] = '\0';
  if (!haveLast) return;

  char think[12];
  if (lastThinkMs == kNoThink) {
    snprintf(think, sizeof(think), "-");
  } else {
    snprintf(think, sizeof(think), "%u", static_cast<unsigned>(lastThinkMs));
  }

  // Microseconds are divided down here rather than stored that way: the overlay is read
  // at arm's length on an e-ink panel, where a digit of precision past the millisecond is
  // noise. The CSV keeps the full resolution.
  char split[16] = {0};
  if (lastAsyncUs > 0) snprintf(split, sizeof(split), " split %lu", static_cast<unsigned long>(lastAsyncUs / 1000));

  snprintf(out, outLen, "%s/%s think %s panel %lu%s ink %u/%u prom %lu pll %02X", modeName(lastRequested),
           modeName(lastActual), think, static_cast<unsigned long>(lastTotalUs / 1000), split,
           static_cast<unsigned>(lastInkScore), static_cast<unsigned>(lastInkDebt),
           static_cast<unsigned long>(promoted), static_cast<unsigned>(activePll));
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

const ModeStats& modeStats(const uint8_t mode) { return stats[mode < kModeCount ? mode : 0]; }

uint32_t promotedCount() { return promoted; }

void setPllByte(const uint8_t pll) { activePll = pll; }

uint8_t pllByte() { return activePll; }

}  // namespace PerfStats
