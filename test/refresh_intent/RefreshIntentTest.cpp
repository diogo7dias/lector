// Host test locking the RefreshIntent -> (verb, waveform) mapping. This is the
// behavior-preservation contract for the present(region, intent) migration: every
// intent must resolve to exactly the refresh verb + waveform the old hand-picked
// call sites used. If any row changes, a screen's refresh behavior changed.
//
// refreshPlanFor() is pure (no HalDisplay/Arduino/panel), so it is host-testable.
// The panel branch (X3 windowed -> full FAST, X3+HALF -> resync) lives BELOW this,
// inside the existing HalDisplay verbs, and is intentionally not re-derived here.

#include <gtest/gtest.h>

#include "RefreshIntent.h"

namespace {

void expectPlan(RefreshIntent intent, RefreshVerb verb, RefreshWave wave) {
  const RefreshPlan p = refreshPlanFor(intent);
  EXPECT_EQ(p.verb, verb);
  EXPECT_EQ(p.wave, wave);
}

TEST(RefreshIntent, MenuNavIsFullFast) { expectPlan(RefreshIntent::MenuNav, RefreshVerb::Buffer, RefreshWave::Fast); }

TEST(RefreshIntent, PageTurnIsAsyncFast) {
  expectPlan(RefreshIntent::PageTurn, RefreshVerb::BufferAsync, RefreshWave::Fast);
}

TEST(RefreshIntent, CleanFrameIsFullHalf) {
  expectPlan(RefreshIntent::CleanFrame, RefreshVerb::Buffer, RefreshWave::Half);
}

TEST(RefreshIntent, DeepCleanIsFullFull) {
  expectPlan(RefreshIntent::DeepClean, RefreshVerb::Buffer, RefreshWave::Full);
}

TEST(RefreshIntent, ProgressBarIsWindowFast) {
  expectPlan(RefreshIntent::ProgressBar, RefreshVerb::Window, RefreshWave::Fast);
}

TEST(RefreshIntent, TransientBandIsWindowAsyncFast) {
  expectPlan(RefreshIntent::TransientBand, RefreshVerb::WindowAsync, RefreshWave::Fast);
}

TEST(RefreshIntent, GrayscaleCleanIsGrayscaleHalf) {
  expectPlan(RefreshIntent::GrayscaleClean, RefreshVerb::GrayscaleBase, RefreshWave::Half);
}

// The one X3 site (xtc grayscale page turn) that must keep the cheap differential
// waveform — merging it into GrayscaleClean would flash + slow every xtc gray page.
TEST(RefreshIntent, GrayscaleDifferentialIsGrayscaleFast) {
  expectPlan(RefreshIntent::GrayscaleDifferential, RefreshVerb::GrayscaleBase, RefreshWave::Fast);
}

}  // namespace
