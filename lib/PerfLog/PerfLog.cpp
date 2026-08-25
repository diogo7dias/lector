#include <Arduino.h>
#include <PerfLog.h>
#include <PerfStats.h>

#include <cstdio>

namespace PerfLog {
namespace {

// Records are batched rather than written per refresh: a card write costs milliseconds,
// and adding those to the very number being measured would make the log a measurement of
// itself. Sized small because sessions are short — a lock and wake is a handful of
// refreshes — and a batch that never fills is a batch that never reaches the card. The
// write lands between refreshes, and a measured X3 refresh is 619 ms, so a few
// milliseconds of card time here is not worth a larger buffer.
constexpr size_t kBatchSize = 16;

struct Record {
  uint32_t ms;
  uint32_t totalUs;
  uint32_t asyncStartUs;
  uint32_t wireUs;  // streaming the frame into controller RAM
  uint32_t waveUs;  // waiting on BUSY while the panel drives
  char screen[14];  // copied, see setScreen
  uint16_t seq;
  uint16_t thinkMs;
  uint16_t inkScore;
  uint16_t inkDebt;
  uint8_t requested;
  uint8_t actual;
  bool turbo;         // this pass ran the panel's cheap partial path
  uint8_t diag;       // PanelDriver::RefreshDiagnostic bits for this pass
  uint16_t settleMs;  // time spent waiting out a panel still driving after its wait returned
};

Record records[kBatchSize];
size_t count = 0;
uint16_t sequence = 0;
char currentScreen[14] = "boot";
LineSink lineSink = nullptr;
CommitSink commitSink = nullptr;
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

void begin(const LineSink sink, const CommitSink commit) {
  lineSink = sink;
  commitSink = commit;
  if (lineSink != nullptr)
    lineSink("seq,ms,screen,req,run,total_us,wire_us,wave_us,async_start_us,think_ms,ink,debt,turbo,diag,settle_ms\n");
  if (commitSink != nullptr) commitSink();
}

bool isActive() { return lineSink != nullptr; }

void setScreen(const char* screenName) {
  if (screenName == nullptr) return;
  snprintf(currentScreen, sizeof(currentScreen), "%s", screenName);
}

void record(const uint8_t requestedMode, const uint8_t actualMode, const uint32_t totalUs, const uint32_t asyncStartUs,
            const uint16_t thinkMs, const uint16_t inkScore, const uint16_t inkDebt, const uint32_t wireUs,
            const uint32_t waveUs, const bool turbo, const uint8_t diag, const uint16_t settleMs) {
  // The whole cost of a build with the setting off, on every refresh it ever performs.
  if (lineSink == nullptr) return;

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
  r.wireUs = wireUs;
  r.waveUs = waveUs;
  r.turbo = turbo;
  r.diag = diag;
  r.settleMs = settleMs;
  snprintf(r.screen, sizeof(r.screen), "%s", currentScreen);
  r.seq = sequence++;
  r.thinkMs = thinkMs;
  r.inkScore = inkScore;
  r.inkDebt = inkDebt;
  r.requested = requestedMode;
  r.actual = actualMode;
}

void note(const char* const text) {
  if (lineSink == nullptr || text == nullptr) return;
  // Flushed first so the annotation lands in sequence with the refreshes around it,
  // rather than ahead of records that were taken before it.
  flush();
  char line[384];
  snprintf(line, sizeof(line), "# %s\n", text);
  if (!lineSink(line)) droppedRecords++;
  if (commitSink != nullptr) commitSink();
}

void flush() {
  if (count == 0 || lineSink == nullptr) return;

  char line[160];
  if (droppedRecords > 0) {
    snprintf(line, sizeof(line), "# %lu records dropped, sink unavailable\n",
             static_cast<unsigned long>(droppedRecords));
    if (lineSink(line)) droppedRecords = 0;
  }
  for (size_t i = 0; i < count; i++) {
    const Record& r = records[i];
    // An absent think time is written as an empty field rather than a number: a
    // spreadsheet averaging the column must not be handed a 65535 to average in.
    char think[8] = {0};
    if (r.thinkMs != PerfStats::kNoThink) snprintf(think, sizeof(think), "%u", static_cast<unsigned>(r.thinkMs));
    snprintf(line, sizeof(line), "%u,%lu,%s,%s,%s,%lu,%lu,%lu,%lu,%s,%u,%u,%u,%u,%u\n", static_cast<unsigned>(r.seq),
             static_cast<unsigned long>(r.ms), r.screen, modeName(r.requested), modeName(r.actual),
             static_cast<unsigned long>(r.totalUs), static_cast<unsigned long>(r.wireUs),
             static_cast<unsigned long>(r.waveUs), static_cast<unsigned long>(r.asyncStartUs), think,
             static_cast<unsigned>(r.inkScore), static_cast<unsigned>(r.inkDebt),
             static_cast<unsigned>(r.turbo ? 1 : 0), static_cast<unsigned>(r.diag), static_cast<unsigned>(r.settleMs));
    if (!lineSink(line)) droppedRecords++;
  }
  // Made durable here rather than per line: the records are worthless if a deep sleep
  // resets the chip before they reach the card.
  if (commitSink != nullptr) commitSink();
  // Cleared unconditionally: a failed line is counted, not retried. Retrying would stall
  // the reader on a bad card, and the count in the file is enough to spot the gap.
  count = 0;
}

}  // namespace PerfLog
