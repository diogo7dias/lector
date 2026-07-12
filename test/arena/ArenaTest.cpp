#include <gtest/gtest.h>

#include <cstdint>

#include "Arena.h"
#include "ArenaVector.h"

namespace {

// A slab holds the ArenaSlab header plus the requested data bytes, so a request
// for exactly `slabSize` bytes fits in the first slab (the header is separate).
constexpr size_t kSlab = 1024;

TEST(Arena, InitAllocatesFirstSlab) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  EXPECT_EQ(arena.used(), 0u);
  void* p = arena.alloc(16);
  ASSERT_NE(p, nullptr);
  EXPECT_GE(arena.used(), 16u);
}

TEST(Arena, AllocationsHonorRequestedAlignment) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  // Ask for an odd size first so the next allocation would be misaligned unless
  // the arena rounds the offset up to the requested alignment. The slab data
  // region begins one ArenaSlab header past a malloc pointer, so it is aligned
  // to the pointer size (>= 4); the arena aligns the offset within that region.
  // 4 and 8 cover every alignment the scratch types actually require.
  ASSERT_NE(arena.alloc(1, 1), nullptr);
  void* a4 = arena.alloc(4, 4);
  ASSERT_NE(a4, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(a4) % 4u, 0u);

  ASSERT_NE(arena.alloc(1, 1), nullptr);
  void* a8 = arena.alloc(8, 8);
  ASSERT_NE(a8, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(a8) % 8u, 0u);
}

TEST(Arena, DistinctAllocationsDoNotOverlap) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  auto* a = static_cast<uint8_t*>(arena.alloc(64, 1));
  auto* b = static_cast<uint8_t*>(arena.alloc(64, 1));
  ASSERT_NE(a, nullptr);
  ASSERT_NE(b, nullptr);
  // b starts at or after the end of a's 64-byte region.
  EXPECT_GE(b, a + 64);
}

TEST(Arena, GrowsToNewSlabWhenFull) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  // Exhaust the first slab, then allocate more — the arena chains a new slab
  // rather than returning nullptr.
  ASSERT_NE(arena.alloc(kSlab, 1), nullptr);
  void* overflow = arena.alloc(256, 1);
  EXPECT_NE(overflow, nullptr);
}

TEST(Arena, OversizedRequestGetsItsOwnSlab) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  // A single request larger than the slab size still succeeds via a bespoke slab.
  void* big = arena.alloc(kSlab * 4, 1);
  EXPECT_NE(big, nullptr);
}

TEST(Arena, ClearResetsUsageButKeepsArenaUsable) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ASSERT_NE(arena.alloc(kSlab, 1), nullptr);  // fill first slab
  ASSERT_NE(arena.alloc(kSlab, 1), nullptr);  // force a second slab
  arena.clear();
  EXPECT_EQ(arena.used(), 0u);
  // Still usable after clear, and the preserved first slab satisfies a request.
  EXPECT_NE(arena.alloc(32, 1), nullptr);
}

TEST(Arena, SaveRestoreRewindsOffset) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ASSERT_NE(arena.alloc(64, 1), nullptr);
  const size_t before = arena.used();
  const ArenaCheckpoint cp = arena.save();

  ASSERT_NE(arena.alloc(128, 1), nullptr);
  EXPECT_GT(arena.used(), before);

  arena.restore(cp);
  EXPECT_EQ(arena.used(), before);
}

TEST(Arena, RestoreFreesSlabsAllocatedAfterCheckpoint) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  const ArenaCheckpoint cp = arena.save();
  // Allocate enough to spill into extra slabs past the checkpoint.
  ASSERT_NE(arena.alloc(kSlab, 1), nullptr);
  ASSERT_NE(arena.alloc(kSlab, 1), nullptr);
  arena.restore(cp);
  EXPECT_EQ(arena.used(), 0u);
  EXPECT_NE(arena.alloc(32, 1), nullptr);
}

TEST(Arena, ArenaNewConstructsObject) {
  struct Pair {
    int a;
    int b;
    Pair(int x, int y) : a(x), b(y) {}
  };
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  Pair* p = arenaNew<Pair>(arena, 3, 4);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->a, 3);
  EXPECT_EQ(p->b, 4);
}

TEST(Arena, ArenaNewArrayValueInitializes) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  int* arr = arenaNewArray<int>(arena, 5);
  ASSERT_NE(arr, nullptr);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(arr[i], 0);
  }
  EXPECT_EQ(arenaNewArray<int>(arena, 0), nullptr);
}

TEST(ArenaVector, PushBackGrowsAndPreservesValues) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<int> v(arena);
  for (int i = 0; i < 100; ++i) {
    ASSERT_TRUE(v.push_back(i * 2));
  }
  ASSERT_EQ(v.size(), 100u);
  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(v[i], i * 2);
  }
}

TEST(ArenaVector, ResizeZeroFillsGrowth) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<uint16_t> v(arena);
  ASSERT_TRUE(v.push_back(7));
  ASSERT_TRUE(v.resize(4));
  ASSERT_EQ(v.size(), 4u);
  EXPECT_EQ(v[0], 7);
  EXPECT_EQ(v[1], 0);
  EXPECT_EQ(v[2], 0);
  EXPECT_EQ(v[3], 0);
}

TEST(ArenaVector, InsertShiftsTail) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<int> v(arena);
  for (int i = 0; i < 4; ++i) {
    ASSERT_TRUE(v.push_back(i));  // 0 1 2 3
  }
  ASSERT_TRUE(v.insert(1, 99));  // 0 99 1 2 3
  ASSERT_EQ(v.size(), 5u);
  const int expected[] = {0, 99, 1, 2, 3};
  for (size_t i = 0; i < v.size(); ++i) {
    EXPECT_EQ(v[i], expected[i]);
  }
  // Out-of-range index is rejected without mutating the vector.
  EXPECT_FALSE(v.insert(99, 1));
  EXPECT_EQ(v.size(), 5u);
}

TEST(ArenaVector, InsertAtEndAppends) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<int> v(arena);
  ASSERT_TRUE(v.push_back(1));
  ASSERT_TRUE(v.insert(1, 2));  // index == size is a valid append
  ASSERT_EQ(v.size(), 2u);
  EXPECT_EQ(v[0], 1);
  EXPECT_EQ(v[1], 2);
}

TEST(ArenaVector, ClearKeepsCapacityResetStorageDrops) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<int> v(arena);
  ASSERT_TRUE(v.push_back(5));
  ASSERT_TRUE(v.push_back(6));
  const size_t cap = v.capacity();
  v.clear();
  EXPECT_TRUE(v.empty());
  EXPECT_EQ(v.capacity(), cap);  // storage retained for reuse
  v.resetStorage();
  EXPECT_EQ(v.capacity(), 0u);
  EXPECT_TRUE(v.empty());
}

TEST(ArenaVector, IterationVisitsAllElements) {
  Arena arena;
  ASSERT_TRUE(arena.init(kSlab));
  ArenaVector<int> v(arena);
  for (int i = 1; i <= 5; ++i) {
    ASSERT_TRUE(v.push_back(i));
  }
  int sum = 0;
  for (int x : v) {
    sum += x;
  }
  EXPECT_EQ(sum, 15);
}

}  // namespace
