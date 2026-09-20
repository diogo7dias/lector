#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "util/ReleaseStorage.h"

TEST(ReleaseStorage, DestroysNestedOwnersAndCapacityAndCanBeRebuilt) {
  // A cached row can own strings, vectors and callback captures. Prove the
  // elements die, not merely that the outer list stops displaying them.
  auto owner = std::make_shared<int>(42);
  std::weak_ptr<int> lifetime = owner;
  std::vector<std::shared_ptr<int>> rows;
  rows.reserve(100);
  rows.push_back(std::move(owner));
  releaseStorage(rows);
  EXPECT_TRUE(lifetime.expired());
  EXPECT_TRUE(rows.empty());
  EXPECT_EQ(rows.capacity(), 0u);

  std::string line(512, 'x');
  line.clear();  // A failed attempt already cleared the text, but kept its heap.
  ASSERT_GE(line.capacity(), 512u);
  releaseStorage(line);
  EXPECT_TRUE(line.empty());
  EXPECT_EQ(line.capacity(), std::string{}.capacity());

  // Returning to Settings, another install attempt, and a repeated release.
  rows.reserve(1);
  rows.push_back(std::make_shared<int>(7));
  line = "Retry";
  EXPECT_EQ(*rows.front(), 7);
  EXPECT_EQ(line, "Retry");
  releaseStorage(rows);
  releaseStorage(line);
  releaseStorage(rows);
  releaseStorage(line);
  EXPECT_EQ(rows.capacity(), 0u);
  EXPECT_TRUE(line.empty());
}
