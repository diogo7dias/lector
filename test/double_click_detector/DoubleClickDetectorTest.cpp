#include "util/DoubleClickDetector.h"

#include <gtest/gtest.h>

using reader_input::DoubleClickDetector;
using Event = DoubleClickDetector::Event;

namespace {

constexpr uint32_t W = DoubleClickDetector::WINDOW_MS;

// Advance the clock without a release edge, returning the first non-None event.
Event idleUntil(DoubleClickDetector& d, uint32_t from, uint32_t to, uint32_t step = 10) {
  for (uint32_t t = from; t <= to; t += step) {
    const Event e = d.update(false, t);
    if (e != Event::None) return e;
  }
  return Event::None;
}

TEST(DoubleClickDetector, IdleReportsNothing) {
  DoubleClickDetector d;
  EXPECT_EQ(idleUntil(d, 0, 5000), Event::None);
  EXPECT_FALSE(d.waiting());
}

TEST(DoubleClickDetector, SingleClickIsHeldBackThenReported) {
  DoubleClickDetector d;
  EXPECT_EQ(d.update(true, 1000), Event::None);
  EXPECT_TRUE(d.waiting());

  // Nothing fires while the window is still open.
  EXPECT_EQ(idleUntil(d, 1010, 1000 + W - 10), Event::None);
  EXPECT_TRUE(d.waiting());

  EXPECT_EQ(d.update(false, 1000 + W), Event::Single);
  EXPECT_FALSE(d.waiting());
}

TEST(DoubleClickDetector, SingleFiresOnlyOnce) {
  DoubleClickDetector d;
  d.update(true, 0);
  ASSERT_EQ(d.update(false, W), Event::Single);
  EXPECT_EQ(idleUntil(d, W + 10, W + 5000), Event::None);
}

TEST(DoubleClickDetector, SecondClickInsideWindowIsDouble) {
  DoubleClickDetector d;
  ASSERT_EQ(d.update(true, 500), Event::None);
  ASSERT_EQ(idleUntil(d, 510, 500 + W - 20), Event::None);
  EXPECT_EQ(d.update(true, 500 + W - 10), Event::Double);
  EXPECT_FALSE(d.waiting());
}

TEST(DoubleClickDetector, DoubleSuppressesTheHeldBackSingle) {
  DoubleClickDetector d;
  d.update(true, 0);
  ASSERT_EQ(d.update(true, 100), Event::Double);
  // The first click must not surface later as a Single.
  EXPECT_EQ(idleUntil(d, 110, 5000), Event::None);
}

TEST(DoubleClickDetector, SecondClickAfterWindowIsTwoSingles) {
  DoubleClickDetector d;
  d.update(true, 0);
  ASSERT_EQ(d.update(false, W), Event::Single);
  ASSERT_EQ(d.update(true, W + 50), Event::None);
  EXPECT_EQ(d.update(false, W + 50 + W), Event::Single);
}

TEST(DoubleClickDetector, ThirdClickStartsAFreshPair) {
  DoubleClickDetector d;
  d.update(true, 0);
  ASSERT_EQ(d.update(true, 50), Event::Double);
  // Third click is a new first click, not a trailing half of the pair.
  ASSERT_EQ(d.update(true, 100), Event::None);
  EXPECT_TRUE(d.waiting());
  EXPECT_EQ(d.update(true, 150), Event::Double);
}

TEST(DoubleClickDetector, ReleaseExactlyAtWindowEdgeIsStillDouble) {
  DoubleClickDetector d;
  d.update(true, 0);
  // The Single only fires on a pass with no release, so a release landing on the very
  // edge is a Double. The two branches must never both claim the same edge.
  EXPECT_EQ(d.update(true, W), Event::Double);
}

TEST(DoubleClickDetector, ResetDropsAPendingClick) {
  DoubleClickDetector d;
  d.update(true, 0);
  ASSERT_TRUE(d.waiting());
  d.reset();
  EXPECT_FALSE(d.waiting());
  EXPECT_EQ(idleUntil(d, 10, 5000), Event::None);
}

TEST(DoubleClickDetector, SurvivesMillisWraparound) {
  DoubleClickDetector d;
  constexpr uint32_t nearMax = 0xFFFFFFFFUL - 100;
  ASSERT_EQ(d.update(true, nearMax), Event::None);
  // 100 ms later the counter has wrapped to 0; the window is not yet closed.
  EXPECT_EQ(d.update(false, 0), Event::None);
  EXPECT_TRUE(d.waiting());
  // W ms after the click, measured across the wrap.
  EXPECT_EQ(d.update(false, W - 100), Event::Single);
}

}  // namespace
