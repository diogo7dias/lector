#include <gtest/gtest.h>

#include "activities/reader/LowMemoryRenderTier.h"

using LowMemoryRenderTier::apply;
using LowMemoryRenderTier::equal;
using LowMemoryRenderTier::kImagesSuppressed;
using LowMemoryRenderTier::kMaxTier;
using LowMemoryRenderTier::Knobs;

namespace {
Knobs fullQuality() {
  return Knobs{/*imageRendering=*/0, /*embeddedStyle=*/true, /*hyphenationEnabled=*/true,
               /*focusReadingEnabled=*/true};
}
}  // namespace

TEST(LowMemoryRenderTier, Tier0IsUnchanged) { EXPECT_TRUE(equal(apply(fullQuality(), 0), fullQuality())); }

TEST(LowMemoryRenderTier, ReductionsAreCumulative) {
  const Knobs t1 = apply(fullQuality(), 1);
  EXPECT_EQ(t1.imageRendering, kImagesSuppressed);
  EXPECT_TRUE(t1.embeddedStyle);
  EXPECT_TRUE(t1.hyphenationEnabled);
  EXPECT_TRUE(t1.focusReadingEnabled);

  const Knobs t2 = apply(fullQuality(), 2);
  EXPECT_EQ(t2.imageRendering, kImagesSuppressed);
  EXPECT_FALSE(t2.embeddedStyle);
  EXPECT_TRUE(t2.hyphenationEnabled);

  const Knobs t3 = apply(fullQuality(), 3);
  EXPECT_FALSE(t3.hyphenationEnabled);
  EXPECT_TRUE(t3.focusReadingEnabled);

  const Knobs t4 = apply(fullQuality(), kMaxTier);
  EXPECT_EQ(t4.imageRendering, kImagesSuppressed);
  EXPECT_FALSE(t4.embeddedStyle);
  EXPECT_FALSE(t4.hyphenationEnabled);
  EXPECT_FALSE(t4.focusReadingEnabled);
}

TEST(LowMemoryRenderTier, TierAboveMaxSaturates) {
  EXPECT_TRUE(equal(apply(fullQuality(), kMaxTier + 5), apply(fullQuality(), kMaxTier)));
}

TEST(LowMemoryRenderTier, NoOpTierIsDetectableWhenBaseAlreadyReduced) {
  // If the user already disabled images, tier 1 (suppress images) is a no-op
  // relative to the base — the ladder loop uses equal() to skip re-building the
  // identical params.
  Knobs base = fullQuality();
  base.imageRendering = kImagesSuppressed;
  EXPECT_TRUE(equal(apply(base, 0), apply(base, 1)));
  // Tier 2 still differs (it also drops CSS).
  EXPECT_FALSE(equal(apply(base, 1), apply(base, 2)));
}

TEST(LowMemoryRenderTier, ApplyDoesNotMutateCaller) {
  const Knobs base = fullQuality();
  (void)apply(base, kMaxTier);
  EXPECT_TRUE(equal(base, fullQuality()));
}
