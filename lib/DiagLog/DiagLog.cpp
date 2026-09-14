#include "DiagLog.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace diaglog {

namespace {
char g_ring[kRingBytes];
size_t g_len = 0;
bool g_pending = false;
uint32_t g_dropped = 0;
// One static scratch line. Every recorder runs on the main task (activities,
// setup, the sleep path), so a single buffer is enough.
// ponytail: no lock; add one if a second task ever records.
char g_line[kLineBytes];

// Drops whole lines from the head until `need` bytes are free.
void makeRoom(size_t need) {
  while (g_len > 0 && kRingBytes - g_len < need) {
    const char* nl = static_cast<const char*>(memchr(g_ring, '\n', g_len));
    const size_t drop = nl ? static_cast<size_t>(nl - g_ring) + 1 : g_len;
    memmove(g_ring, g_ring + drop, g_len - drop);
    g_len -= drop;
    g_dropped++;
  }
}

void appendLine(const char* text, size_t len) {
  if (len + 1 > kRingBytes) len = kRingBytes - 1;
  makeRoom(len + 1);
  memcpy(g_ring + g_len, text, len);
  g_len += len;
  g_ring[g_len++] = '\n';
}

// Howard Hinnant's days_from_civil; valid for the years a device clock can hold.
uint32_t daysFromCivil(int y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return static_cast<uint32_t>(era * 146097 + static_cast<int>(doe) - 719468);
}

void civilFromDays(uint32_t z, int& y, unsigned& m, unsigned& d) {
  z += 719468;
  const uint32_t era = z / 146097;
  const unsigned doe = z - era * 146097;
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int>(yoe + era * 400);
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}

// Start of the next entry at or after `from`, or `len` when there is none.
size_t nextEntry(const char* file, size_t len, size_t from) {
  const size_t prefix = strlen(kEntryPrefix);
  for (size_t i = from; i + prefix <= len; ++i) {
    if ((i == 0 || file[i - 1] == '\n') && memcmp(file + i, kEntryPrefix, prefix) == 0) return i;
  }
  return len;
}
}  // namespace

void formatUtc(const uint32_t utc, char* out, const size_t cap) {
  if (utc < kClockValidFrom) {
    snprintf(out, cap, "unset");
    return;
  }
  int y;
  unsigned m, d;
  civilFromDays(utc / 86400, y, m, d);
  const uint32_t sod = utc % 86400;
  snprintf(out, cap, "%04d-%02u-%02uT%02u:%02u:%02uZ", y, m, d, sod / 3600, (sod / 60) % 60, sod % 60);
}

uint32_t utcFromCivil(const uint16_t year, const uint8_t month, const uint8_t day, const uint8_t hour,
                      const uint8_t minute, const uint8_t second) {
  if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60) {
    return 0;
  }
  return daysFromCivil(year, month, day) * 86400u + hour * 3600u + minute * 60u + second;
}

uint32_t parseEntryUtc(const char* line, const size_t len) {
  const char* end = static_cast<const char*>(memchr(line, '\n', len));
  const size_t lineLen = end ? static_cast<size_t>(end - line) : len;
  const char* key = " utc=";
  const size_t keyLen = strlen(key);
  for (size_t i = 0; i + keyLen < lineLen; ++i) {
    if (memcmp(line + i, key, keyLen) != 0) continue;
    unsigned y, mo, d, h, mi, s;
    // The value is followed by a space or the end of the line; sscanf stops on
    // the first non-matching byte, and "unset" fails the first %u.
    char buf[24];
    const size_t rest = lineLen - (i + keyLen);
    const size_t take = rest < sizeof(buf) - 1 ? rest : sizeof(buf) - 1;
    memcpy(buf, line + i + keyLen, take);
    buf[take] = '\0';
    if (sscanf(buf, "%4u-%2u-%2uT%2u:%2u:%2uZ", &y, &mo, &d, &h, &mi, &s) != 6) return 0;
    return utcFromCivil(static_cast<uint16_t>(y), static_cast<uint8_t>(mo), static_cast<uint8_t>(d),
                        static_cast<uint8_t>(h), static_cast<uint8_t>(mi), static_cast<uint8_t>(s));
  }
  return 0;
}

void clear() {
  g_len = 0;
  g_pending = false;
  g_dropped = 0;
}

size_t size() { return g_len; }
const char* data() { return g_ring; }
bool pending() { return g_pending; }
void markPending() { g_pending = true; }
uint32_t droppedLines() { return g_dropped; }

void note(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vnote(fmt, args);
  va_end(args);
}

void vnote(const char* fmt, va_list args) {
  int n = vsnprintf(g_line, sizeof(g_line), fmt, args);
  if (n < 0) return;
  if (static_cast<size_t>(n) >= sizeof(g_line)) n = static_cast<int>(sizeof(g_line)) - 1;
  appendLine(g_line, static_cast<size_t>(n));
}

void beginEntry(const char* kind, const char* fields, const Clock now) {
  char when[24];
  formatUtc(now.utc, when, sizeof(when));
  note("%s%s%s%s utc=%s up=%us", kEntryPrefix, kind, fields ? " " : "", fields ? fields : "", when,
       static_cast<unsigned>(now.uptimeSeconds));
}

size_t retain(char* file, size_t len, const uint32_t nowUtc, const size_t cap) {
  // Rule 1: nothing before the first entry survives.
  const size_t first = nextEntry(file, len, 0);
  memmove(file, file + first, len - first);
  len -= first;

  // Rule 2: age, sparing the newest entry.
  const bool nowKnown = nowUtc >= kClockValidFrom;
  size_t out = 0;
  size_t at = 0;
  while (at < len) {
    const size_t next = nextEntry(file, len, at + 1);
    const uint32_t entryUtc = parseEntryUtc(file + at, next - at);
    const bool isNewest = next >= len;
    const bool old = nowKnown && entryUtc != 0 && nowUtc > entryUtc && nowUtc - entryUtc > kRetainSeconds;
    if (isNewest || !old) {
      memmove(file + out, file + at, next - at);
      out += next - at;
    }
    at = next;
  }
  len = out;

  // Rule 3: byte cap, oldest first.
  size_t start = 0;
  while (len - start > cap && start < len) {
    start = nextEntry(file, len, start + 1);
  }
  if (len - start > cap) start = len;  // a single entry over the cap: nothing old fits
  memmove(file, file + start, len - start);
  return len - start;
}

void imageLabel(const char* path, char* out, const size_t cap) {
  if (!path || !*path) {
    snprintf(out, cap, "unknown");
    return;
  }
  const char* name = path;
  if (*name == '/') name++;
  if (strchr(name, '/') != nullptr) {
    snprintf(out, cap, "(file in a folder)");
    return;
  }
  snprintf(out, cap, "/%.40s", name);
}

}  // namespace diaglog
