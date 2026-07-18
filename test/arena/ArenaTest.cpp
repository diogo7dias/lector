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

// --- Slab-fallback ladder (initWithFallback) ------------------------------
//
// On the device a heavy chapter can leave ~95KB free but no single block larger
// than a few KB. initWithFallback() starts the arena in that fragmented heap by
// trying smaller first slabs. Host malloc never fails, so these tests inject a
// failing allocator via Arena::testAllocHook (ARENA_ENABLE_TEST_HOOKS) to
// simulate the fragmentation.

// slab requests (sizeof(ArenaSlab) + dataSize) at or above this fail.
size_t gSlabFailAtOrAbove = SIZE_MAX;

void* fragmentedAlloc(size_t bytes) { return bytes >= gSlabFailAtOrAbove ? nullptr : ::malloc(bytes); }

struct ArenaFallback : ::testing::Test {
  void SetUp() override { Arena::testAllocHook = fragmentedAlloc; }
  void TearDown() override {
    Arena::testAllocHook = nullptr;
    gSlabFailAtOrAbove = SIZE_MAX;
  }
};

TEST_F(ArenaFallback, UsesPreferredSlabWhenHeapHealthy) {
  gSlabFailAtOrAbove = SIZE_MAX;  // nothing fails
  Arena arena;
  ASSERT_TRUE(arena.initWithFallback(4096, 1024));
  EXPECT_EQ(arena.slabSize, 4096u);  // no fallback needed
}

TEST_F(ArenaFallback, FallsBackToSmallerSlabWhenLargeBlockUnavailable) {
  // Only blocks smaller than ~1.5KB can be allocated: the 4096 and 2048 rungs
  // fail, the 1024 rung succeeds.
  gSlabFailAtOrAbove = 1500;
  Arena arena;
  ASSERT_TRUE(arena.initWithFallback(4096, 1024));
  EXPECT_EQ(arena.slabSize, 1024u);
  // The arena is fully usable on the fallback slab.
  EXPECT_NE(arena.alloc(512, 1), nullptr);
}

TEST_F(ArenaFallback, FallsBackToMiddleRung) {
  // Blocks up to ~3KB succeed: 4096 fails, 2048 succeeds.
  gSlabFailAtOrAbove = 3000;
  Arena arena;
  ASSERT_TRUE(arena.initWithFallback(4096, 1024));
  EXPECT_EQ(arena.slabSize, 2048u);
}

TEST_F(ArenaFallback, ReturnsFalseWhenEvenMinSlabFails) {
  gSlabFailAtOrAbove = 1;  // every allocation fails
  Arena arena;
  EXPECT_FALSE(arena.initWithFallback(4096, 1024));
  EXPECT_EQ(arena.head, nullptr);  // left uninitialized, no leak
}

TEST_F(ArenaFallback, HonorsMinBytesFloor) {
  // A 1024-data slab (~1048 bytes with header) fails, but a 512-data slab
  // (~536 bytes) would succeed. The ladder must stop at the 1024 floor and
  // NOT descend to 512, so it returns false rather than using a tiny slab.
  gSlabFailAtOrAbove = 600;
  Arena arena;
  EXPECT_FALSE(arena.initWithFallback(4096, 1024));
}

// ── initWithBuffer: lay a non-owning slab inside a caller buffer ──────────────

TEST(ArenaBuffer, AllocatesFromCallerBufferAndDoesNotFreeIt) {
  // A heap buffer we own and free ourselves: if the arena ever ::free'd it,
  // the delete[] below would double-free (caught under sanitizers). Passing
  // proves the arena treats the buffer as non-owning.
  constexpr size_t kLen = 4096;
  auto* buffer = new uint8_t[kLen];

  {
    Arena arena;
    ASSERT_TRUE(arena.initWithBuffer(buffer, kLen));
    EXPECT_TRUE(arena.headBorrowed_);

    void* p = arena.alloc(64, 8);
    ASSERT_NE(p, nullptr);
    // The allocation lands inside the caller buffer.
    const uintptr_t pv = reinterpret_cast<uintptr_t>(p);
    EXPECT_GE(pv, reinterpret_cast<uintptr_t>(buffer));
    EXPECT_LT(pv, reinterpret_cast<uintptr_t>(buffer) + kLen);
    EXPECT_GE(arena.used(), 64u);
  }  // arena.release() runs here; must NOT free `buffer`.

  delete[] buffer;  // our free; a prior arena ::free would have double-freed
}

TEST(ArenaBuffer, AlignsHeaderInsideMisalignedBuffer) {
  // Feed a deliberately misaligned start; the slab header must be placed at an
  // aligned address (RISC-V faults on unaligned multi-byte access).
  constexpr size_t kRaw = 4096;
  auto* raw = new uint8_t[kRaw];
  uint8_t* misaligned = raw + 1;  // 1-byte-off start

  Arena arena;
  ASSERT_TRUE(arena.initWithBuffer(misaligned, kRaw - 1));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(arena.head) % alignof(ArenaSlab), 0u);
  void* p = arena.alloc(32, 4);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 4u, 0u);

  delete[] raw;
}

TEST(ArenaBuffer, RejectsBufferTooSmallForHeader) {
  uint8_t tiny[4];
  Arena arena;
  EXPECT_FALSE(arena.initWithBuffer(tiny, sizeof(tiny)));
  EXPECT_EQ(arena.head, nullptr);
}

TEST(ArenaBuffer, RejectsNullBuffer) {
  Arena arena;
  EXPECT_FALSE(arena.initWithBuffer(nullptr, 4096));
}

TEST(ArenaBuffer, ArenaVectorFillsFromBorrowedBuffer) {
  // End-to-end: an ArenaVector (the layout scratch pattern) bumps within the
  // borrowed buffer and reads back correctly.
  constexpr size_t kLen = 2048;
  auto* buffer = new uint8_t[kLen];

  {
    Arena arena;
    ASSERT_TRUE(arena.initWithBuffer(buffer, kLen));
    ArenaVector<uint16_t> v(arena);
    for (uint16_t i = 0; i < 100; ++i) ASSERT_TRUE(v.push_back(i));
    ASSERT_EQ(v.size(), 100u);
    for (uint16_t i = 0; i < 100; ++i) EXPECT_EQ(v[i], i);
  }

  delete[] buffer;
}

}  // namespace
