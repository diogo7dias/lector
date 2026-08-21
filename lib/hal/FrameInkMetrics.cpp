#include "FrameInkMetrics.h"

namespace {

// Black pixels are ZERO bits: the framebuffer is cleared to 0xFF for white (see
// HalDisplay::clearScreen). So the ink in a byte is 8 minus its population count.
//
// A table rather than __builtin_popcount: the C3 is RV32IMC, which has no population
// count instruction, so the builtin expands to the usual shift-and-mask sequence. A
// 256-byte table in flash is one load per byte instead.
constexpr uint8_t kInkInByte[256] = {
    8, 7, 7, 6, 7, 6, 6, 5, 7, 6, 6, 5, 6, 5, 5, 4, 7, 6, 6, 5, 6, 5, 5, 4, 6, 5, 5, 4, 5, 4, 4, 3, 7, 6, 6, 5, 6,
    5, 5, 4, 6, 5, 5, 4, 5, 4, 4, 3, 6, 5, 5, 4, 5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3, 2, 7, 6, 6, 5, 6, 5, 5, 4, 6, 5,
    5, 4, 5, 4, 4, 3, 6, 5, 5, 4, 5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3, 2, 6, 5, 5, 4, 5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3,
    2, 5, 4, 4, 3, 4, 3, 3, 2, 4, 3, 3, 2, 3, 2, 2, 1, 7, 6, 6, 5, 6, 5, 5, 4, 6, 5, 5, 4, 5, 4, 4, 3, 6, 5, 5, 4,
    5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3, 2, 6, 5, 5, 4, 5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3, 2, 5, 4, 4, 3, 4, 3, 3, 2, 4,
    3, 3, 2, 3, 2, 2, 1, 6, 5, 5, 4, 5, 4, 4, 3, 5, 4, 4, 3, 4, 3, 3, 2, 5, 4, 4, 3, 4, 3, 3, 2, 4, 3, 3, 2, 3, 2,
    2, 1, 5, 4, 4, 3, 4, 3, 3, 2, 4, 3, 3, 2, 3, 2, 2, 1, 4, 3, 3, 2, 3, 2, 2, 1, 3, 2, 2, 1, 2, 1, 1, 0,
};

// How the two halves of the score are weighted, in tenths.
//
// COVERAGE (how much of the screen changed at all) carries most of it, because on this
// panel a differential pass drives every pixel it touches whether that pixel is going
// dark or light. A page turn rewrites the whole reading area and must not score as
// nothing simply because the new page happens to hold as much ink as the old one.
//
// CHURN (how much the ink totals moved) carries the rest, and is what separates an
// ordinary page turn from an inversion or a cover swap: those change coverage AND totals,
// so they reach the top of the scale where a page turn sits at roughly a third of it.
constexpr uint32_t kCoverageWeight = 3;
constexpr uint32_t kChurnWeight = 7;
constexpr uint32_t kWeightTotal = kCoverageWeight + kChurnWeight;

constexpr uint32_t kFnvOffsetBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

}  // namespace

void FrameInkMetrics::reset() {
  primed_ = false;
  prevBands_ = 0;
}

FrameInkMetrics::Result FrameInkMetrics::update(const uint8_t* const frameBuffer, const uint16_t widthBytes,
                                                const uint16_t height) {
  Result result;
  if (frameBuffer == nullptr || widthBytes == 0 || height == 0) return result;

  const uint32_t bandCount32 = (static_cast<uint32_t>(height) + BAND_ROWS - 1) / BAND_ROWS;
  if (bandCount32 > MAX_BANDS) return result;  // unmeasurable, so charged as worst case

  const uint8_t bandCount = static_cast<uint8_t>(bandCount32);
  result.totalBands = bandCount;
  result.firstChangedBand = bandCount;

  const uint32_t bandBytes = static_cast<uint32_t>(widthBytes) * BAND_ROWS;
  const uint32_t frameBytes = static_cast<uint32_t>(widthBytes) * height;
  const uint32_t frameInkCapacity = frameBytes * 8;

  const bool comparable = primed_ && prevBands_ == bandCount;

  uint32_t changedBands = 0;
  uint32_t inkChurn = 0;

  for (uint8_t band = 0; band < bandCount; band++) {
    const uint32_t start = band * bandBytes;
    uint32_t end = start + bandBytes;
    if (end > frameBytes) end = frameBytes;  // last band on a height that is not a multiple of 8

    uint32_t hash = kFnvOffsetBasis;
    uint32_t ink = 0;
    for (uint32_t i = start; i < end; i++) {
      const uint8_t byte = frameBuffer[i];
      ink += kInkInByte[byte];
      hash = (hash ^ byte) * kFnvPrime;
    }

    const uint16_t inkCount = static_cast<uint16_t>(ink);
    if (comparable && prevHash_[band] == hash) {
      // Identical band: no pixels driven here, nothing to charge.
    } else {
      changedBands++;
      if (band < result.firstChangedBand) result.firstChangedBand = band;
      result.lastChangedBand = band;
      if (comparable) {
        inkChurn += prevInk_[band] > inkCount ? static_cast<uint32_t>(prevInk_[band] - inkCount)
                                              : static_cast<uint32_t>(inkCount - prevInk_[band]);
      } else {
        // Nothing to compare against, so the churn cannot be known. Charge the band's
        // full ink: an unmeasured frame must never look cheaper than a measured one.
        inkChurn += inkCount;
      }
    }

    prevHash_[band] = hash;
    prevInk_[band] = inkCount;
  }

  prevBands_ = bandCount;
  primed_ = true;
  result.changedBands = static_cast<uint8_t>(changedBands);

  if (!comparable) {
    // First frame of a session, or the first after a reset: the panel's real contents are
    // unknown, so this pass is charged in full.
    result.score = MAX_SCORE;
    return result;
  }

  const uint32_t coverage = (changedBands * MAX_SCORE) / bandCount;
  uint32_t churn = frameInkCapacity == 0 ? 0 : (inkChurn * MAX_SCORE) / frameInkCapacity;
  if (churn > MAX_SCORE) churn = MAX_SCORE;

  const uint32_t score = (coverage * kCoverageWeight + churn * kChurnWeight) / kWeightTotal;
  result.score = static_cast<uint16_t>(score > MAX_SCORE ? MAX_SCORE : score);
  return result;
}
