#include <HalDisplay.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

DeviceProfile g_profile{};

bool hasEvent(const std::string& ev) {
  const auto& v = einkCallLog().events;
  return std::find(v.begin(), v.end(), ev) != v.end();
}

int indexOf(const std::string& ev) {
  const auto& v = einkCallLog().events;
  auto it = std::find(v.begin(), v.end(), ev);
  return it == v.end() ? -1 : static_cast<int>(it - v.begin());
}

}  // namespace

DeviceProfile deviceProfileFromHardware() { return g_profile; }

class AsyncPageTurnTest : public ::testing::Test {
 protected:
  HalDisplay hal;
  void init(DeviceProfile p) {
    g_profile = p;
    einkCallLog().clear();
    hal.begin(/*seamless=*/true, wake_classify::WakeupReason::PowerButton);
    einkCallLog().clear();
  }
};

// SSD1677 single-buffer async FAST leaves controller RED RAM stale unless waitRefreshComplete()
// resyncs the baseline from the untouched framebuffer before the next differential update.
TEST_F(AsyncPageTurnTest, Ssd1677AsyncFastResyncsRedBaselineOnWait) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"displayBufferAsyncNoShadow:FAST"}));

  einkCallLog().clear();
  hal.waitRefreshComplete();
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"waitRefreshComplete", "cleanupGrayscaleBuffers:fb"}));

  // A second wait is a no-op and must not rewrite RED again.
  einkCallLog().clear();
  hal.waitRefreshComplete();
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"waitRefreshComplete"}));
}

// SSD1677 HALF/FULL already writes both BW and RED planes before activating the waveform.
TEST_F(AsyncPageTurnTest, Ssd1677AsyncHalfSkipsRedundantBaselineSync) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.displayBufferAsync(HalDisplay::HALF_REFRESH);
  einkCallLog().clear();
  hal.waitRefreshComplete();
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"waitRefreshComplete"}));
}

// Blocking FAST following an async FAST on SSD1677 must still drain and resync RED first so
// the blocking differential does not diff against a 2-pages-old RED plane.
TEST_F(AsyncPageTurnTest, Ssd1677BlockingFastAfterAsyncFastResyncsBeforeSubmit) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  einkCallLog().clear();
  hal.displayBuffer(HalDisplay::FAST_REFRESH, false);

  const int waitIdx = indexOf("waitRefreshComplete");
  const int syncIdx = indexOf("cleanupGrayscaleBuffers:fb");
  const int dispIdx = indexOf("displayBuffer:FAST");
  ASSERT_GE(waitIdx, 0);
  ASSERT_GE(syncIdx, 0);
  ASSERT_GE(dispIdx, 0);
  EXPECT_LT(waitIdx, syncIdx);
  EXPECT_LT(syncIdx, dispIdx);
}

// Blocking HALF following an async FAST overwrites RED anyway, so it drains without extra RED write.
TEST_F(AsyncPageTurnTest, Ssd1677BlockingHalfAfterAsyncFastSkipsRedundantSync) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  einkCallLog().clear();
  hal.displayBuffer(HalDisplay::HALF_REFRESH, false);

  EXPECT_FALSE(hasEvent("cleanupGrayscaleBuffers:fb"));
  const int waitIdx = indexOf("waitRefreshComplete");
  const int dispIdx = indexOf("displayBuffer:HALF");
  ASSERT_GE(waitIdx, 0);
  ASSERT_GE(dispIdx, 0);
  EXPECT_LT(waitIdx, dispIdx);
}

// Lending the framebuffer storage must sync RED while the framebuffer still holds the page.
TEST_F(AsyncPageTurnTest, Ssd1677LendStorageResyncsRedBeforeLending) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  einkCallLog().clear();
  uint32_t sz = 0;
  ASSERT_NE(hal.lendFrameBufferStorage(&sz), nullptr);
  const int syncIdx = indexOf("cleanupGrayscaleBuffers:fb");
  const int lendIdx = indexOf("lendBuildStorage");
  ASSERT_GE(syncIdx, 0);
  ASSERT_GE(lendIdx, 0);
  EXPECT_LT(syncIdx, lendIdx);
  hal.returnFrameBufferStorage();
}

// X3 (UC8253/UC8279) syncs DTM1 inside driver displayFinish(), so HalDisplay must not call cleanupGrayscaleBuffers.
TEST_F(AsyncPageTurnTest, X3AsyncFastDoesNotDoubleSyncBaseline) {
  DeviceProfile x3{};
  x3.isX3 = true;
  init(x3);

  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  einkCallLog().clear();
  hal.waitRefreshComplete();
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"waitRefreshComplete"}));
}

// X3 (UC8253) HALF/FULL runs multi-pass conditioning inside displayFinish(); keep it synchronous
// so cleanup waveforms do not stall main-loop input 2 s later during idle panel power-off.
TEST_F(AsyncPageTurnTest, X3AsyncHalfRunsSynchronouslySoPostConditionDoesNotDefer) {
  DeviceProfile x3{};
  x3.isX3 = true;
  init(x3);

  hal.displayBufferAsync(HalDisplay::HALF_REFRESH);
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"requestResync:1", "displayBuffer:HALF"}));
}

// Night mode (inverted) takes the SDK's blocking path, so waitRefreshComplete needs no RED resync.
TEST_F(AsyncPageTurnTest, Ssd1677InvertedAsyncDoesNotResyncRed) {
  DeviceProfile x4{};
  x4.controllerIsSsd1677 = true;
  init(x4);

  hal.setInverted(true);
  einkCallLog().clear();
  hal.displayBufferAsync(HalDisplay::FAST_REFRESH);
  einkCallLog().clear();
  hal.waitRefreshComplete();
  EXPECT_EQ(einkCallLog().events, (std::vector<std::string>{"waitRefreshComplete"}));
}
