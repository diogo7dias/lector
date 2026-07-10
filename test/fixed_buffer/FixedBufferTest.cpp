#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>

#include "FixedBuffer.h"

TEST(FixedBuffer, StoresCapacityInlineWithoutHeapOwnerState) {
  using Buffer = FixedBuffer<uint8_t, 4096>;
  static_assert(std::is_trivially_destructible_v<Buffer>);
  static_assert(sizeof(Buffer) == 4096);

  Buffer buffer;
  EXPECT_EQ(buffer.size(), 4096u);
  buffer.data()[0] = 0x12;
  buffer.data()[4095] = 0x34;
  EXPECT_EQ(buffer.data()[0], 0x12);
  EXPECT_EQ(buffer.data()[4095], 0x34);
}
