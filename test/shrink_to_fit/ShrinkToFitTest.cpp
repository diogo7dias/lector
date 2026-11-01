#include <Memory.h>
#include <gtest/gtest.h>

#include <deque>
#include <string>
#include <vector>

TEST(ShrinkToFit, TrimsVectorThatShrinkToFitKeeps) {
  std::vector<int> v;
  v.reserve(100);
  v.push_back(7);
  v.shrink_to_fit();
  EXPECT_EQ(v.capacity(), 100u) << "libstdc++ shrink_to_fit is expected to be a no-op without exceptions";
  shrinkToFit(v);
  EXPECT_EQ(v.capacity(), 1u);
  EXPECT_EQ(v.front(), 7);
}

TEST(ShrinkToFit, FreesEmptyVectorAndKeepsDequeContents) {
  std::vector<std::string> v(64);
  v.clear();
  shrinkToFit(v);
  EXPECT_EQ(v.capacity(), 0u);

  std::deque<std::string> d{"a", "b"};
  shrinkToFit(d);
  ASSERT_EQ(d.size(), 2u);
  EXPECT_EQ(d[1], "b");
}
