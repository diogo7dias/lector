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
  // Same offset rule as HalClock::formatTime, so the journal's date matches the clock shown.
  const uint8_t q = SETTINGS.clockUtcOffsetQ > 104 ? 104 : SETTINGS.clockUtcOffsetQ;
  const uint8_t zone = SETTINGS.clockTimeZone;
  const int offset =
      zone != 0 && zone < clock_zones::COUNT ? clock_zones::offsetMinutes(zone, utc) : (static_cast<int>(q) - 48) * 15;
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

bool summarize(Summary& summary, const size_t tailBytes) {
  Civil today;
  if (localNow(today)) summary.setToday(today);

  HalFile f;
  if (!Storage.exists(PATH) || !Storage.openFileForRead("JRN", PATH, f)) return false;
  const size_t size = f.fileSize();
  bool skipToNewline = false;
  if (tailBytes > 0 && size > tailBytes) {
    f.seekSet(size - tailBytes);
    skipToNewline = true;  // landed mid-line
  }

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
    size_t start = 0;
    if (skipToNewline) {
      const char* nl = static_cast<const char*>(memchr(buf, '\n', len));
      if (!nl) {
        have = 0;
        continue;
      }
      start = static_cast<size_t>(nl - buf) + 1;
      skipToNewline = false;
    }
    size_t carry = summary.addText(buf + start, len - start);
    // A line longer than any we write is not ours: drop it rather than overflow.
    if (carry > MAX_LINE) carry = 0;
    memmove(buf, buf + len - carry, carry);
    have = carry;
  }
  return true;
}

}  // namespace reading_journal
