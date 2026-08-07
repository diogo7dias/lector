#include <PerfLog.h>

#if defined(PERF_LOG) && PERF_LOG

#include <Arduino.h>

#include <cstdio>

namespace PerfLog {
namespace {

// Records are batched rather than written per refresh: a card write costs milliseconds,
// and adding those to the very number being measured would make the log a measurement of
// itself. 128 records covers a 30-page run several times over.
constexpr size_t kBatchSize = 128;

struct Record {
  uint32_t ms;
  uint32_t totalUs;
  uint32_t asyncStartUs;
  char screen[14];  // copied, see setScreen
  uint16_t seq;
  uint8_t requested;
  uint8_t actual;
};

Record records[kBatchSize];
size_t count = 0;
uint16_t sequence = 0;
char currentScreen[14] = "boot";
LineSink lineSink = nullptr;
// Set when a record is lost. Reported in the log itself on the next successful write, so
// a gap in the sequence numbers is never silently unexplained.
uint32_t droppedRecords = 0;

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

void begin(const LineSink sink) {
  lineSink = sink;
  if (lineSink != nullptr) lineSink("seq,ms,screen,req,run,total_us,async_start_us\n");
}

void setScreen(const char* screenName) {
  if (screenName == nullptr) return;
  snprintf(currentScreen, sizeof(currentScreen), "%s", screenName);
}

void record(const uint8_t requestedMode, const uint8_t actualMode, const uint32_t totalUs,
            const uint32_t asyncStartUs) {
  if (count >= kBatchSize) flush();
  // Still full after a flush means the sink is failing. Count the loss and carry on
  // rather than blocking: a measurement build must never be able to wedge the reader.
  if (count >= kBatchSize) {
    droppedRecords++;
    sequence++;
    return;
  }

  Record& r = records[count++];
  r.ms = millis();
  r.totalUs = totalUs;
  r.asyncStartUs = asyncStartUs;
  snprintf(r.screen, sizeof(r.screen), "%s", currentScreen);
  r.seq = sequence++;
  r.requested = requestedMode;
  r.actual = actualMode;
}

void flush() {
  if (count == 0 || lineSink == nullptr) return;

  char line[128];
  if (droppedRecords > 0) {
    snprintf(line, sizeof(line), "# %lu records dropped, sink unavailable\n",
             static_cast<unsigned long>(droppedRecords));
    if (lineSink(line)) droppedRecords = 0;
  }
  for (size_t i = 0; i < count; i++) {
    const Record& r = records[i];
    snprintf(line, sizeof(line), "%u,%lu,%s,%s,%s,%lu,%lu\n", static_cast<unsigned>(r.seq),
             static_cast<unsigned long>(r.ms), r.screen, modeName(r.requested), modeName(r.actual),
             static_cast<unsigned long>(r.totalUs), static_cast<unsigned long>(r.asyncStartUs));
    if (!lineSink(line)) droppedRecords++;
  }
  // Cleared unconditionally: a failed line is counted, not retried. Retrying would stall
  // the reader on a bad card, and the count in the file is enough to spot the gap.
  count = 0;
}

}  // namespace PerfLog

#endif  // PERF_LOG
