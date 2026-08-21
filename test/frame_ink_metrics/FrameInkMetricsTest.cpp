#include <gtest/gtest.h>

#include <vector>

#include "FrameInkMetrics.h"

namespace {

// X4 geometry: 800 x 480, so 100 bytes per row and 60 bands of 8 rows.
constexpr uint16_t kWidthBytes = 100;
constexpr uint16_t kHeight = 480;

std::vector<uint8_t> whiteFrame() { return std::vector<uint8_t>(kWidthBytes * kHeight, 0xFF); }

// Fills one band with black. Bands are BAND_ROWS rows tall, counting from the top.
void blackenBand(std::vector<uint8_t>& fb, const uint8_t band) {
  const size_t start = static_cast<size_t>(band) * kWidthBytes * FrameInkMetrics::BAND_ROWS;
  for (size_t i = 0; i < static_cast<size_t>(kWidthBytes) * FrameInkMetrics::BAND_ROWS; i++) fb[start + i] = 0x00;
}

// Roughly what a page of body text looks like: a few pixels set in most rows, spread over
// the whole reading area rather than concentrated in one band.
std::vector<uint8_t> textLikeFrame(const uint8_t seed) {
  std::vector<uint8_t> fb = whiteFrame();
  for (uint16_t row = 0; row < kHeight; row++) {
    for (uint16_t col = 0; col < kWidthBytes; col += 3) {
      fb[static_cast<size_t>(row) * kWidthBytes + col] = static_cast<uint8_t>(0xF0 ^ ((row + col + seed) & 0x0F));
    }
  }
  return fb;
}

FrameInkMetrics::Result score(FrameInkMetrics& metrics, const std::vector<uint8_t>& fb) {
  return metrics.update(fb.data(), kWidthBytes, kHeight);
}

}  // namespace

// The first frame of a session has nothing to compare against: the panel may be holding
// anything at all. It must be charged in full rather than assumed cheap.
TEST(FrameInkMetrics, TheFirstFrameIsChargedAsWorstCase) {
  FrameInkMetrics metrics;
  const auto result = score(metrics, whiteFrame());
  EXPECT_EQ(result.score, FrameInkMetrics::MAX_SCORE);
  EXPECT_EQ(result.totalBands, 60);
}

TEST(FrameInkMetrics, AnIdenticalFrameCostsNothing) {
  FrameInkMetrics metrics;
  const auto fb = whiteFrame();
  score(metrics, fb);
  const auto result = score(metrics, fb);
  EXPECT_EQ(result.score, 0);
  EXPECT_EQ(result.changedBands, 0);
}

// The case the whole thing exists for: a full inversion drives every pixel on the panel
// and must sit at the top of the scale.
TEST(FrameInkMetrics, AWholeFrameInversionScoresTheMaximum) {
  FrameInkMetrics metrics;
  score(metrics, whiteFrame());
  const std::vector<uint8_t> black(kWidthBytes * kHeight, 0x00);
  const auto result = score(metrics, black);
  EXPECT_EQ(result.score, FrameInkMetrics::MAX_SCORE);
  EXPECT_EQ(result.changedBands, 60);
}

// A menu selection moving one row is the cheapest real interaction there is, and must not
// be charged anything like a page turn — otherwise navigating a list forces cleans.
TEST(FrameInkMetrics, AOneBandChangeIsCheap) {
  FrameInkMetrics metrics;
  auto fb = whiteFrame();
  score(metrics, fb);
  blackenBand(fb, 10);
  const auto result = score(metrics, fb);
  EXPECT_EQ(result.changedBands, 1);
  EXPECT_EQ(result.firstChangedBand, 10);
  EXPECT_EQ(result.lastChangedBand, 10);
  EXPECT_LT(result.score, 100);
}

// A page turn changes nearly every band while barely moving the ink totals. It has to land
// well above a menu move (it drives the whole reading area) and well below an inversion
// (it does not drive every pixel from one rail to the other).
TEST(FrameInkMetrics, APageTurnSitsBetweenAMenuMoveAndAnInversion) {
  FrameInkMetrics metrics;
  score(metrics, textLikeFrame(0));
  const auto result = score(metrics, textLikeFrame(7));
  EXPECT_GT(result.score, 100);
  EXPECT_LT(result.score, 600);
}

// Ordering is the property the policy actually depends on: whatever the exact numbers,
// heavier content must never score below lighter content.
TEST(FrameInkMetrics, ScoresAreOrderedByHowMuchInkMoves) {
  uint16_t menuScore = 0;
  {
    FrameInkMetrics metrics;
    auto fb = whiteFrame();
    score(metrics, fb);
    blackenBand(fb, 3);
    menuScore = score(metrics, fb).score;
  }

  uint16_t pageScore = 0;
  {
    FrameInkMetrics metrics;
    score(metrics, textLikeFrame(0));
    pageScore = score(metrics, textLikeFrame(7)).score;
  }

  uint16_t inversionScore = 0;
  {
    FrameInkMetrics metrics;
    score(metrics, whiteFrame());
    inversionScore = score(metrics, std::vector<uint8_t>(kWidthBytes * kHeight, 0x00)).score;
  }

  EXPECT_LT(menuScore, pageScore);
  EXPECT_LT(pageScore, inversionScore);
}

// After a reset the retained signature no longer describes the glass, so the next frame
// must go back to being charged in full rather than compared against a stale frame.
TEST(FrameInkMetrics, AResetMakesTheNextFrameWorstCaseAgain) {
  FrameInkMetrics metrics;
  const auto fb = whiteFrame();
  score(metrics, fb);
  ASSERT_EQ(score(metrics, fb).score, 0);
  metrics.reset();
  EXPECT_EQ(score(metrics, fb).score, FrameInkMetrics::MAX_SCORE);
}

// A frame that cannot be measured must be charged as if it were the worst case: silently
// scoring it zero would let an unmeasurable panel accumulate ghosting unchecked.
TEST(FrameInkMetrics, AnUnmeasurableFrameIsChargedInFull) {
  FrameInkMetrics metrics;
  EXPECT_EQ(metrics.update(nullptr, kWidthBytes, kHeight).score, FrameInkMetrics::MAX_SCORE);

  const std::vector<uint8_t> fb(kWidthBytes * 8, 0xFF);
  // Taller than MAX_BANDS * BAND_ROWS: no room to hold a signature for it.
  EXPECT_EQ(metrics.update(fb.data(), kWidthBytes, FrameInkMetrics::MAX_BANDS * FrameInkMetrics::BAND_ROWS + 8).score,
            FrameInkMetrics::MAX_SCORE);
}

// X3 geometry, which is the taller panel and the one nearest the band ceiling.
TEST(FrameInkMetrics, HandlesTheX3Geometry) {
  FrameInkMetrics metrics;
  const std::vector<uint8_t> fb(99 * 528, 0xFF);
  const auto result = metrics.update(fb.data(), 99, 528);
  EXPECT_EQ(result.totalBands, 66);
}
