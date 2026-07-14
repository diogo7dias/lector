#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "WakeFrameHandoff.h"

namespace {

std::vector<uint8_t> makeFrame(uint8_t seed, size_t len = 48000) {
  std::vector<uint8_t> frame(len);
  for (size_t i = 0; i < len; i++) {
    frame[i] = static_cast<uint8_t>(seed + i * 31);
  }
  return frame;
}

class WakeFrameHandoffTest : public ::testing::Test {
 protected:
  void SetUp() override { wake_frame::disarm(); }
  void TearDown() override { wake_frame::disarm(); }
};

TEST_F(WakeFrameHandoffTest, StartsDisarmed) {
  EXPECT_FALSE(wake_frame::isArmed());
  const auto frame = makeFrame(1);
  EXPECT_FALSE(wake_frame::consumeIfMatch(frame.data(), frame.size()));
}

TEST_F(WakeFrameHandoffTest, MatchingBufferConsumesAndSkips) {
  const auto frame = makeFrame(7);
  wake_frame::arm(wake_frame::hashBuffer(frame.data(), frame.size()));
  EXPECT_TRUE(wake_frame::isArmed());
  EXPECT_TRUE(wake_frame::consumeIfMatch(frame.data(), frame.size()));
  EXPECT_FALSE(wake_frame::isArmed());
}

TEST_F(WakeFrameHandoffTest, MismatchConsumesButDoesNotSkip) {
  const auto restored = makeFrame(7);
  auto rendered = restored;
  rendered[1234] ^= 0x01;  // one pixel differs (e.g. battery digit)
  wake_frame::arm(wake_frame::hashBuffer(restored.data(), restored.size()));
  EXPECT_FALSE(wake_frame::consumeIfMatch(rendered.data(), rendered.size()));
  EXPECT_FALSE(wake_frame::isArmed());
}

TEST_F(WakeFrameHandoffTest, IsOneShotEvenOnMatch) {
  const auto frame = makeFrame(3);
  wake_frame::arm(wake_frame::hashBuffer(frame.data(), frame.size()));
  EXPECT_TRUE(wake_frame::consumeIfMatch(frame.data(), frame.size()));
  EXPECT_FALSE(wake_frame::consumeIfMatch(frame.data(), frame.size()));
}

TEST_F(WakeFrameHandoffTest, DisarmDropsPendingHandoff) {
  const auto frame = makeFrame(9);
  wake_frame::arm(wake_frame::hashBuffer(frame.data(), frame.size()));
  wake_frame::disarm();
  EXPECT_FALSE(wake_frame::consumeIfMatch(frame.data(), frame.size()));
}

TEST_F(WakeFrameHandoffTest, HashDetectsLengthDifference) {
  const auto frame = makeFrame(5);
  const uint32_t full = wake_frame::hashBuffer(frame.data(), frame.size());
  const uint32_t truncated = wake_frame::hashBuffer(frame.data(), frame.size() - 1);
  EXPECT_NE(full, truncated);
}

}  // namespace
