// Host tests for the buildscratch registry (lib/Memory/BuildScratch.cpp): the
// hand-off between GfxRenderer::FrameBufferLoan (lender) and build-phase
// consumers. Locks the invariants the loan depends on: single claimant,
// minLen gating, claim-after-release, and a clean slate after reclaim().

#include <BuildScratch.h>
#include <gtest/gtest.h>

#include <cstdint>

namespace {

class BuildScratchTest : public ::testing::Test {
 protected:
  // Every test leaves the registry empty (it is process-global state).
  void TearDown() override { buildscratch::reclaim(); }

  uint8_t storage[1024] = {};
};

TEST_F(BuildScratchTest, ClaimWithoutLendReturnsNull) { EXPECT_EQ(buildscratch::claim(1), nullptr); }

TEST_F(BuildScratchTest, ClaimReturnsLentBlockAndLength) {
  buildscratch::lend(storage, sizeof(storage));
  size_t len = 0;
  uint8_t* p = buildscratch::claim(512, &len);
  EXPECT_EQ(p, storage);
  EXPECT_EQ(len, sizeof(storage));
}

TEST_F(BuildScratchTest, ClaimRefusedWhenBlockTooSmall) {
  buildscratch::lend(storage, sizeof(storage));
  EXPECT_EQ(buildscratch::claim(sizeof(storage) + 1), nullptr);
  // The failed claim must not consume the block.
  EXPECT_EQ(buildscratch::claim(sizeof(storage)), storage);
}

TEST_F(BuildScratchTest, SecondClaimRefusedUntilRelease) {
  buildscratch::lend(storage, sizeof(storage));
  uint8_t* first = buildscratch::claim(1);
  ASSERT_EQ(first, storage);
  EXPECT_EQ(buildscratch::claim(1), nullptr);

  buildscratch::release(first);
  EXPECT_EQ(buildscratch::claim(1), storage);
}

TEST_F(BuildScratchTest, ReleaseOfForeignPointerIsIgnored) {
  buildscratch::lend(storage, sizeof(storage));
  ASSERT_EQ(buildscratch::claim(1), storage);

  uint8_t other[16] = {};
  buildscratch::release(other);    // not the lent block: must be a no-op
  buildscratch::release(nullptr);  // ditto
  EXPECT_EQ(buildscratch::claim(1), nullptr);
}

TEST_F(BuildScratchTest, ReclaimEmptiesRegistry) {
  buildscratch::lend(storage, sizeof(storage));
  buildscratch::reclaim();
  EXPECT_EQ(buildscratch::claim(1), nullptr);
}

TEST_F(BuildScratchTest, SecondLendIgnoredWhileFirstActive) {
  uint8_t other[64] = {};
  buildscratch::lend(storage, sizeof(storage));
  buildscratch::lend(other, sizeof(other));  // logged + ignored
  size_t len = 0;
  EXPECT_EQ(buildscratch::claim(1, &len), storage);
  EXPECT_EQ(len, sizeof(storage));
}

TEST_F(BuildScratchTest, LendAgainAfterReclaimWorks) {
  buildscratch::lend(storage, sizeof(storage));
  buildscratch::reclaim();
  uint8_t other[64] = {};
  buildscratch::lend(other, sizeof(other));
  EXPECT_EQ(buildscratch::claim(1), other);
}

}  // namespace
