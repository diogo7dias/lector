#pragma once

// The one reading-percent unit every reader shows (Home badge, status bar, in-book
// menu, screenshot overlay): a whole percent 0-100 of the pages fully passed through.
// On 0-based `page` of `total` that is page / (total - 1), rounded DOWN, so the first
// page reads 0 and only the last page reads 100. A one-page run is passed on its only
// page (100); an empty run (total 0) has nothing to pass (0). Pure: no Epub, no Storage.
namespace reading_percent {

// Share of a `total`-page run passed through while on `page`, 0..1.
constexpr float pageFraction(const int page, const int total) {
  if (total <= 0) return 0.0f;
  if (total == 1) return 1.0f;
  const float f = static_cast<float>(page) / static_cast<float>(total - 1);
  return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

// A 0..1 fraction as a whole percent, rounded down. The tiny nudge keeps a float that
// should be exactly 1 (the EPUB byte-weighted sum at the end of the last chapter) from
// landing on 99.
constexpr int toPercent(const float fraction) {
  const int p = static_cast<int>(fraction * 100.0f + 0.001f);
  return p < 0 ? 0 : (p > 100 ? 100 : p);
}

constexpr int pagePercent(const int page, const int total) { return toPercent(pageFraction(page, total)); }

}  // namespace reading_percent
