// Host tests for makeSharedNoThrow (lib/Memory/Memory.h), the nothrow
// std::make_shared replacement introduced to stop layout-path allocations from
// abort()ing under -fno-exceptions on a starved device heap. The OOM-returns-
// null path cannot be exercised on the host (new(nothrow) does not fail here),
// so these lock the success semantics the callers rely on: a valid shared_ptr,
// correct construction/forwarding, working reference counting, and destructor
// invocation. The device relies on the return being null-checkable, which the
// type guarantees.

#include <Memory.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

namespace {

struct Tracked {
  int a;
  std::string b;
  static int liveCount;
  Tracked(int a, std::string b) : a(a), b(std::move(b)) { ++liveCount; }
  ~Tracked() { --liveCount; }
};
int Tracked::liveCount = 0;

TEST(MakeSharedNoThrow, ConstructsAndForwardsArgs) {
  auto p = makeSharedNoThrow<Tracked>(42, std::string("hello"));
  ASSERT_TRUE(p);
  EXPECT_EQ(p->a, 42);
  EXPECT_EQ(p->b, "hello");
}

TEST(MakeSharedNoThrow, IsNullCheckable) {
  // The whole point: the result is a shared_ptr whose truthiness the callers
  // test. On the host the allocation always succeeds, so it is non-null here.
  std::shared_ptr<int> p = makeSharedNoThrow<int>(7);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 7);
}

TEST(MakeSharedNoThrow, ReferenceCountingWorks) {
  auto p = makeSharedNoThrow<int>(1);
  ASSERT_TRUE(p);
  EXPECT_EQ(p.use_count(), 1);
  {
    auto q = p;
    EXPECT_EQ(p.use_count(), 2);
  }
  EXPECT_EQ(p.use_count(), 1);
}

TEST(MakeSharedNoThrow, DestroysObjectWhenLastRefDrops) {
  Tracked::liveCount = 0;
  {
    auto p = makeSharedNoThrow<Tracked>(1, std::string("x"));
    ASSERT_TRUE(p);
    EXPECT_EQ(Tracked::liveCount, 1);
  }
  EXPECT_EQ(Tracked::liveCount, 0);
}

TEST(MakeSharedNoThrow, MoveOnlyConstructionSupported) {
  auto up = makeUniqueNoThrow<int>(99);
  ASSERT_TRUE(up);
  auto p = makeSharedNoThrow<std::unique_ptr<int>>(std::move(up));
  ASSERT_TRUE(p);
  ASSERT_TRUE(*p);
  EXPECT_EQ(**p, 99);
}

}  // namespace
