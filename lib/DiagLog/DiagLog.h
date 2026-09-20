#pragma once

#include <cstdarg>
#include <cstddef>
#include <cstdint>

// The RAM half of the on-card diagnostics file: a bounded text buffer that the
// firmware appends lines to, and the retention rule that decides which of the
// file's existing entries survive the next write.
//
// Nothing here touches storage. The card is only written by the owner of this
// buffer (src/Diagnostics.cpp) at a handful of flush points: an install attempt
// ending, an update failure, an abnormal boot, bounded Wi-Fi checkpoints,
// a deep sleep with unflushed lines, or the file being downloaded. Recording a
// line is a memcpy into a static buffer, so nothing on a page turn, a wake or a
// render can cost a card write.
//
// Why a text buffer rather than structs: the file is the product. Formatting
// once, at record time, into the bytes that will be written keeps the flush a
// plain copy and the wrap rule a matter of dropping whole lines.
//
// Host-testable: no Arduino, no ESP-IDF, no heap.
namespace diaglog {

// Static buffer size. An SD install attempt with its partition table is about
// 750 bytes, an OTA heap-gate line about 90, a boot record about 250. Failures
// flush immediately, so the buffer only ever has to hold one attempt plus the
// informational lines that led up to it. 2048 bytes is 0.5% of the C3's DRAM.
constexpr size_t kRingBytes = 2048;
// Longest single line. A partition line is ~60 bytes, an entry header ~120.
constexpr size_t kLineBytes = 192;
// The file is rewritten at every flush and never grows past this: two SD
// attempts with partition tables and a handful of OTA failures, small enough to
// paste whole into a chat.
constexpr size_t kFileCapBytes = 6144;
// Both on-card header lines plus the optional RAM-drop notice.
constexpr size_t kHeaderReserveBytes = 320;
// Wi-Fi checkpoints: a finite burst to locate the first blocked call, then a
// shared rate limit for boundaries and heartbeat, including millis() rollover.
constexpr uint32_t kWifiImmediateWrites = 48;
constexpr uint32_t kWifiIntervalMs = 5000;
constexpr bool wifiCheckpointDue(uint32_t writes, uint32_t nowMs, uint32_t lastMs, bool boundary) {
  return (boundary && writes < kWifiImmediateWrites) || nowMs - lastMs >= kWifiIntervalMs;
}
static_assert(kRingBytes + kHeaderReserveBytes < kFileCapBytes);
// Entries older than this are dropped when the file is rewritten.
constexpr uint32_t kRetainSeconds = 2u * 24u * 3600u;
// Seconds since 1970 for 2020-01-01T00:00:00Z. Anything earlier is an unset clock.
constexpr uint32_t kClockValidFrom = 1577836800u;
// Prefix of every entry's first line; retention splits the file on it.
constexpr const char* kEntryPrefix = "=== ";

// A moment, as the firmware knows it. utc == 0 means the clock is unset.
struct Clock {
  uint32_t utc;
  uint32_t uptimeSeconds;
};

// Formats `utc` as 2026-09-14T10:22:31Z into `out` (needs 21 bytes), or
// "unset" when utc is 0 or earlier than kClockValidFrom.
void formatUtc(uint32_t utc, char* out, size_t cap);
// Seconds since 1970 for a civil UTC date, 0 for an impossible one.
uint32_t utcFromCivil(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute, uint8_t second);
// The utc= field of an entry header line, 0 when absent or unset.
uint32_t parseEntryUtc(const char* line, size_t len);

// --- the buffer ------------------------------------------------------------

// Drops everything, including the pending flag.
void clear();
// Bytes currently buffered, and a pointer to them (contiguous, not terminated).
size_t size();
const char* data();
// True once something worth keeping on the card was recorded: an attempt, a
// failure, an abnormal boot. Purely informational lines (a heap gate that
// passed) do not set it, so a session that only ever checked for an update and
// then locked writes nothing.
bool pending();
void markPending();
// Lines dropped from the head because the buffer was full, since the last clear.
uint32_t droppedLines();

// Appends one line (a newline is added). When the line does not fit, whole
// lines are dropped from the head until it does, oldest first. Formats with
// snprintf into a static line buffer: no heap, no stack buffer.
void note(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void vnote(const char* fmt, va_list args);
// Starts an entry: "=== <kind> <fields> utc=<...> up=<n>s". `fields` may be null.
void beginEntry(const char* kind, const char* fields, Clock now);

// --- retention -------------------------------------------------------------

// Compacts an existing file's bytes in place so that what remains, plus the
// buffered lines, fits in `cap` bytes. Returns the kept length. Rules, in order:
//   1. Bytes before the first entry (the header, or a partial entry left by a
//      tail read) are dropped; the header is regenerated at write time.
//   2. An entry whose utc and `now` are both set and that is older than
//      kRetainSeconds is dropped, except the newest entry in the file, which
//      survives on any clock: a clock that jumped forward must not empty the
//      file a user is about to send. An entry with no utc, or a `now` that is
//      unset, cannot be judged and is kept; an entry from the future (the clock
//      went backward) is not old and is kept. Rule 3 bounds all of those.
//   3. Oldest entries are dropped until the kept bytes fit in `cap`.
size_t retain(char* file, size_t len, uint32_t nowUtc, size_t cap);

// --- privacy ---------------------------------------------------------------

// What the file may say about an image path. A file at the card root keeps its
// name (that is where firmware.bin goes); anything in a folder is reported as
// "(file in a folder)", because folders on the card are the user's library.
// Names longer than 40 bytes are cut. `out` needs 48 bytes.
void imageLabel(const char* path, char* out, size_t cap);

}  // namespace diaglog
