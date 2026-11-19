#include "ReadingJournalStore.h"

#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>

#include <cstring>

#include "CrossPointSettings.h"

namespace reading_journal {

bool localNow(Civil& out) {
  uint16_t year;
  uint8_t month, day, hour, minute;
  if (!halClock.getDateTime(year, month, day, hour, minute)) return false;
  const int64_t utc = clock_zones::utcSeconds(year, month, day, hour, minute);
  // Same zone rule as HalClock::formatTime, so the journal's date matches the clock shown.
  const int offset = clock_zones::offsetMinutes(SETTINGS.clockTimezone, SETTINGS.clockDst, utc);
  out = localTime(utc, offset);
  return true;
}

void append(Session& session, const uint32_t nowMs, const char* title) {
  session.clock.touch(nowMs);
  const uint32_t seconds = session.clock.seconds();
  if (seconds < MIN_SESSION_S) return;

  Civil now;
  const bool dated = localNow(now);
  char line[MAX_LINE];
  const size_t len =
      formatLine(line, sizeof(line), dated ? &now : nullptr, seconds, session.pages, session.finished, title);
  if (len == 0) return;

  // Appended in one write, no .part: a reset mid-write leaves at most one cut line,
  // which the parser drops, and never loses the lines before it. A cut line has no
  // newline, so end it first or this record would be glued onto it and lost too.
  Storage.ensureDirectoryExists(DIR);
  HalFile f = Storage.open(PATH, O_RDWR | O_CREAT | O_APPEND);
  bool ok = f.isOpen();
  const size_t size = ok ? f.fileSize() : 0;
  char last = '\n';
  if (ok && size > 0 && f.seekSet(size - 1)) f.read(&last, 1);
  if (ok && last != '\n') ok = f.write("\n", 1) == 1;
  if (!ok || f.write(line, len) != len) {
    LOG_ERR("JRN", "Failed to append reading journal");
    return;
  }
  LOG_DBG("JRN", "Session %lus %lu pages", static_cast<unsigned long>(seconds),
          static_cast<unsigned long>(session.pages));
}

namespace {

// Streams the journal through `fold.addText`, never holding it whole.
template <class Fold>
bool foldJournal(Fold& fold) {
  HalFile f;
  if (!Storage.exists(PATH) || !Storage.openFileForRead("JRN", PATH, f)) return false;

  // Heap, not stack: 448 bytes is over the stack budget. Held only while folding.
  constexpr size_t CHUNK = 256;
  const auto bufPtr = makeUniqueNoThrow<char[]>(CHUNK + MAX_LINE);
  if (!bufPtr) {
    LOG_ERR("JRN", "OOM: %u bytes", static_cast<unsigned>(CHUNK + MAX_LINE));
    return false;
  }
  char* const buf = bufPtr.get();
  size_t have = 0;
  for (;;) {
    const int n = f.read(buf + have, CHUNK);
    if (n <= 0) break;
    const size_t len = have + static_cast<size_t>(n);
    size_t carry = fold.addText(buf, len);
    // A line longer than any we write is not ours: drop it rather than overflow.
    if (carry > MAX_LINE) carry = 0;
    memmove(buf, buf + len - carry, carry);
    have = carry;
  }
  return true;
}

}  // namespace

bool summarize(Summary& summary) {
  Civil today;
  if (localNow(today)) summary.setToday(today);
  return foldJournal(summary);
}

bool bookHistory(BookHistory& history) { return foldJournal(history); }

}  // namespace reading_journal
