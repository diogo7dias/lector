#include <gtest/gtest.h>

#include <cstdint>

#include "SerializationBounds.h"

TEST(SerializationBounds, AcceptsLengthWithinLimitAndRemainingBytes) {
  EXPECT_TRUE(serialization::isStringLengthValid(512, 1024, 4096));
  EXPECT_TRUE(serialization::isStringLengthValid(4096, 4096, 4096));
}

TEST(SerializationBounds, RejectsLengthOverConfiguredLimit) {
  EXPECT_FALSE(serialization::isStringLengthValid(4097, 8192, 4096));
  EXPECT_FALSE(serialization::isStringLengthValid(UINT32_MAX, UINT32_MAX, 4096));
}

TEST(SerializationBounds, RejectsLengthBeyondRemainingInput) {
  EXPECT_FALSE(serialization::isStringLengthValid(513, 512, 4096));
}

TEST(SerializationBounds, EmptyStringNeedsNoAllocationHeadroom) {
  EXPECT_TRUE(serialization::hasAllocationHeadroom(0, 0, 2048));
  EXPECT_FALSE(serialization::hasAllocationHeadroom(1, 2048, 2048));
  EXPECT_TRUE(serialization::hasAllocationHeadroom(1, 2049, 2048));
}

TEST(SerializationBounds, RejectsArrayCountLargerThanRemainingInput) {
  EXPECT_TRUE(serialization::isArrayCountValid(4, 16, sizeof(uint32_t), 100));
  EXPECT_FALSE(serialization::isArrayCountValid(5, 16, sizeof(uint32_t), 100));
}

TEST(SerializationBounds, RejectsArrayCountAboveConfiguredLimit) {
  EXPECT_FALSE(serialization::isArrayCountValid(101, 1000, sizeof(uint32_t), 100));
}
