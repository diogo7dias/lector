#pragma once
#include <cstdint>

// Time-left estimates for the status bar, from the reader's own pace this sitting.
// Pure integer/float math, no Arduino, so test/reading_time pins it on the host.
namespace reading_time {

// Gaps outside this window are not reading: a flick through pages, or the book left open.
constexpr uint32_t MIN_PAGE_MS = 2000;
constexpr uint32_t MAX_PAGE_MS = 10UL * 60 * 1000;
// Pages timed before an estimate shows, so the first quick turn cannot set it.
constexpr uint16_t MIN_SAMPLES = 3;

// ponytail: session-only average; persist it per device if a fresh book's blank start annoys.
struct PageTimer {
  uint32_t totalMs = 0;
  uint16_t samples = 0;
  uint32_t lastTurnMs = 0;
  bool haveLast = false;

  // Every page turn moves the anchor; only a forward turn inside the window is a sample.
  void onTurn(const uint32_t nowMs, const bool forward) {
    if (forward && haveLast) {
      const uint32_t gap = nowMs - lastTurnMs;
      if (gap >= MIN_PAGE_MS && gap <= MAX_PAGE_MS && samples < UINT16_MAX) {
        totalMs += gap;
        ++samples;
      }
    }
    lastTurnMs = nowMs;
    haveLast = true;
  }

  // Average milliseconds per page, or -1 before MIN_SAMPLES pages were timed.
  int32_t msPerPage() const { return samples >= MIN_SAMPLES ? static_cast<int32_t>(totalMs / samples) : -1; }
};

// Whole minutes for `pages` at `msPerPage`, rounded up so a last page never reads 0; -1 when unknown.
inline int minutesFor(const int pages, const int32_t msPerPage) {
  if (msPerPage < 0 || pages < 0) return -1;
  if (pages == 0) return 0;
  const int64_t ms = static_cast<int64_t>(pages) * msPerPage;
  return static_cast<int>((ms + 59999) / 60000);
}

// Pages left in the book: the rest of this chapter, plus the book after it at this chapter's
// pages per unit of progress. chapterStart/End are the chapter's book-progress fractions (0..1).
// -1 when the chapter has no measurable span to scale from.
inline int bookPagesLeft(const int chapterPagesLeft, const int chapterPages, const float chapterStart,
                         const float chapterEnd) {
  const float span = chapterEnd - chapterStart;
  if (chapterPages <= 0 || span <= 0.0001f) return -1;
  const float after = chapterEnd < 1.0f ? (1.0f - chapterEnd) * static_cast<float>(chapterPages) / span : 0.0f;
  return chapterPagesLeft + static_cast<int>(after + 0.5f);
}

}  // namespace reading_time
