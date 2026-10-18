#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>

#include <ClockZones.h>

// Reading journal: /stats/journal.txt, one tab-separated line per reading session,
// appended when a book closes. Plain text so the card can be read, edited or graphed
// anywhere. Pure functions, no Arduino, so test/reading_journal pins them on the host.
//
//   2026-10-18T14:05<TAB>seconds<TAB>pages<TAB>done<TAB>title
//
// The first field is the local time the session ended, or "-" when the clock was never
// set: the session still counts toward the totals, never toward a day.
namespace reading_journal {

constexpr const char* DIR = "/stats";
constexpr const char* PATH = "/stats/journal.txt";
// A page left open longer than this is not reading: the clock stops for that gap.
constexpr uint32_t IDLE_CUT_MS = 5UL * 60 * 1000;
// Shorter sittings (a peek at the page, a wrong book) are not written.
constexpr uint32_t MIN_SESSION_S = 60;
constexpr size_t MAX_LINE = 192;

// Time spent with a book open, counted between touches (open, page turns, close). A gap
// over IDLE_CUT_MS adds nothing: the book was left open, not read.
struct SessionClock {
  uint32_t ms = 0;
  uint32_t lastMs = 0;
  bool started = false;

  void touch(const uint32_t nowMs) {
    if (started && nowMs - lastMs <= IDLE_CUT_MS) ms += nowMs - lastMs;
    lastMs = nowMs;
    started = true;
  }
  uint32_t seconds() const { return ms / 1000; }
};

// One sitting with one book: what a reader keeps from open to close.
struct Session {
  SessionClock clock;
  uint32_t pages = 0;  // forward turns
  bool finished = false;

  void turn(const uint32_t nowMs, const bool forward) {
    clock.touch(nowMs);
    if (forward && pages < UINT32_MAX) ++pages;
  }
};

struct Civil {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
};

// Inverse of clock_zones::daysFromCivil (H. Hinnant's civil_from_days).
inline Civil civilFromDays(int64_t z) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  Civil c;
  c.day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
  c.month = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
  c.year = static_cast<int>(yoe + era * 400 + (c.month <= 2));
  return c;
}

// Local wall time for a UTC instant shifted by offsetMinutes.
inline Civil localTime(const int64_t utcSeconds, const int offsetMinutes) {
  const int64_t local = utcSeconds + static_cast<int64_t>(offsetMinutes) * 60;
  const int64_t days = (local >= 0 ? local : local - 86399) / 86400;
  Civil c = civilFromDays(days);
  const int64_t secOfDay = local - days * 86400;
  c.hour = static_cast<int>(secOfDay / 3600);
  c.minute = static_cast<int>(secOfDay % 3600 / 60);
  return c;
}

// One journal line, newline included. `when` nullptr writes an undated session. Tabs and
// line breaks in the title become spaces so the line stays one record. Returns the
// length, or 0 when the buffer is too small for even an empty title.
inline size_t formatLine(char* buf, const size_t size, const Civil* when, const uint32_t seconds,
                         const uint32_t pages, const bool done, const char* title) {
  char stamp[20] = "-";
  if (when) {
    snprintf(stamp, sizeof(stamp), "%04d-%02d-%02dT%02d:%02d", when->year, when->month, when->day, when->hour,
             when->minute);
  }
  const int head = snprintf(buf, size, "%s\t%lu\t%lu\t%d\t", stamp, static_cast<unsigned long>(seconds),
                            static_cast<unsigned long>(pages), done ? 1 : 0);
  if (head < 0 || static_cast<size_t>(head) + 2 > size) return 0;
  size_t n = static_cast<size_t>(head);
  for (const char* p = title ? title : ""; *p && n + 2 < size; ++p) {
    buf[n++] = (*p == '\t' || *p == '\n' || *p == '\r') ? ' ' : *p;
  }
  buf[n++] = '\n';
  buf[n] = '\0';
  return n;
}

struct Entry {
  bool dated = false;
  int64_t day = 0;  // clock_zones::daysFromCivil of the local date, when dated
  Civil date;
  uint32_t seconds = 0;
  uint32_t pages = 0;
  bool done = false;
};

namespace detail {
inline bool parseUint(const std::string_view s, uint32_t& out) {
  if (s.empty() || s.size() > 9) return false;
  uint32_t v = 0;
  for (const char c : s) {
    if (c < '0' || c > '9') return false;
    v = v * 10 + static_cast<uint32_t>(c - '0');
  }
  out = v;
  return true;
}
inline std::string_view nextField(std::string_view& rest) {
  const size_t tab = rest.find('\t');
  const std::string_view field = rest.substr(0, tab);
  rest = tab == std::string_view::npos ? std::string_view{} : rest.substr(tab + 1);
  return field;
}
}  // namespace detail

// False for anything that is not a whole record (a line cut short by a reset mid-append,
// a hand edit gone wrong), so one bad line costs only itself.
inline bool parseLine(std::string_view line, Entry& e) {
  if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
  std::string_view rest = line;
  const std::string_view stamp = detail::nextField(rest);
  const std::string_view secs = detail::nextField(rest);
  const std::string_view pages = detail::nextField(rest);
  const size_t doneEnd = rest.find('\t');
  if (doneEnd == std::string_view::npos) return false;  // the title field must be there
  const std::string_view done = rest.substr(0, doneEnd);

  Entry out;
  if (!detail::parseUint(secs, out.seconds) || !detail::parseUint(pages, out.pages)) return false;
  if (done != "0" && done != "1") return false;
  out.done = done == "1";
  if (stamp != "-") {
    uint32_t y, mo, d, h, mi;
    if (stamp.size() != 16 || stamp[4] != '-' || stamp[7] != '-' || stamp[10] != 'T' || stamp[13] != ':' ||
        !detail::parseUint(stamp.substr(0, 4), y) || !detail::parseUint(stamp.substr(5, 2), mo) ||
        !detail::parseUint(stamp.substr(8, 2), d) || !detail::parseUint(stamp.substr(11, 2), h) ||
        !detail::parseUint(stamp.substr(14, 2), mi) || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) {
      return false;
    }
    out.dated = true;
    out.date = Civil{static_cast<int>(y), static_cast<int>(mo), static_cast<int>(d), static_cast<int>(h),
                     static_cast<int>(mi)};
    out.day = clock_zones::daysFromCivil(out.date.year, mo, d);
  }
  e = out;
  return true;
}

struct Totals {
  uint32_t seconds = 0;
  uint32_t pages = 0;
  uint32_t sessions = 0;
  uint32_t finished = 0;

  void add(const Entry& e) {
    seconds += e.seconds;
    pages += e.pages;
    ++sessions;
    if (e.done) ++finished;
  }
};

// Everything the Stats screen and the sleep line show, folded one entry at a time so
// the journal is streamed, never held. Without today's date only `all` fills in.
struct Summary {
  bool haveToday = false;
  Civil today;
  int64_t todayDay = 0;
  Totals week;  // today and the six days before it
  Totals year;  // this calendar year
  Totals all;
  uint16_t monthMinutes[31] = {};  // this month, by day of month

  void setToday(const Civil& t) {
    haveToday = true;
    today = t;
    todayDay = clock_zones::daysFromCivil(t.year, t.month, t.day);
  }

  void add(const Entry& e) {
    all.add(e);
    if (!haveToday || !e.dated) return;
    if (e.day <= todayDay && e.day > todayDay - 7) week.add(e);
    if (e.date.year == today.year) year.add(e);
    // A day after today means the clock was wrong when it was written: no square for it.
    if (e.date.year == today.year && e.date.month == today.month && e.day <= todayDay) {
      const uint32_t m = monthMinutes[e.date.day - 1] + (e.seconds + 59) / 60;
      monthMinutes[e.date.day - 1] = m > UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(m);
    }
  }

  // Splits a buffer of journal text into lines and adds each whole one. A trailing
  // fragment with no newline is returned (its length) for the caller to carry over.
  size_t addText(const char* text, const size_t len) {
    size_t start = 0;
    for (size_t i = 0; i < len; ++i) {
      if (text[i] != '\n') continue;
      Entry e;
      if (parseLine(std::string_view(text + start, i - start), e)) add(e);
      start = i + 1;
    }
    return len - start;
  }
};

// "12m", "3h05": the status bar's own duration style, so no unit needs translating.
inline void formatDuration(char* buf, const size_t size, const uint32_t seconds) {
  const uint32_t minutes = seconds / 60;
  if (minutes < 60) {
    snprintf(buf, size, "%lum", static_cast<unsigned long>(minutes));
  } else {
    snprintf(buf, size, "%luh%02lu", static_cast<unsigned long>(minutes / 60), static_cast<unsigned long>(minutes % 60));
  }
}

inline int daysInMonth(const int year, const int month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return month == 2 && leap ? 29 : DAYS[month - 1];
}

}  // namespace reading_journal
