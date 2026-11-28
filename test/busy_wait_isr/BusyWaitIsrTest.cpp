#include <freertos/semphr.h>
#include <gtest/gtest.h>

#include "EpdBus.h"
#include "hal/BusyWaitPowerPolicy.h"

namespace {

int g_beginHooks = 0;
int g_endHooks = 0;
void onBeginHook() { g_beginHooks++; }
void onEndHook() { g_endHooks++; }

class BusyWaitIsrTest : public ::testing::Test {
 protected:
  freeink::EpdBus bus;

  void SetUp() override {
    g_hw = {};
    g_sem = {};
    g_beginHooks = 0;
    g_endHooks = 0;
    const freeink::EpdPins pins{12, 11, 13, 18, 14, 6};
    bus.begin(pins, 16000000, freeink::BusyPolarity::UcIdleHigh);
    bus.setBusyWaitHooks(onBeginHook, onEndHook);
  }
};

TEST_F(BusyWaitIsrTest, UcIdleHighUsesRisingEdgeIsrWithoutDelaySpin) {
  g_hw.busyLevel = LOW;
  // Simulate the panel raising BUSY_N after 450 ms while the task sleeps on the semaphore.
  g_sem.onBlockingTake = [](TickType_t) -> BaseType_t {
    g_hw.nowMs += 450;
    g_hw.busyLevel = HIGH;
    if (g_hw.attachedIsr) g_hw.attachedIsr();
    return pdTRUE;
  };
  // Safety net for the old polling loop: raise BUSY after 30 polls so the test terminates and fails cleanly.
  g_hw.onDelay = [](unsigned long) {
    if (g_hw.delayCalls >= 30) g_hw.busyLevel = HIGH;
  };

  bus.waitRefreshComplete("uc_test");

  EXPECT_EQ(g_hw.attachCount, 1);
  EXPECT_EQ(g_hw.lastAttachMode, RISING);
  EXPECT_EQ(g_hw.detachCount, 1);
  EXPECT_EQ(g_sem.blockingTakes, 1);
  EXPECT_LE(g_hw.delayCalls, 1) << "UcIdleHigh waitRefreshComplete must not spin delay(1) at 1 kHz";
  EXPECT_EQ(g_beginHooks, 1);
  EXPECT_EQ(g_endHooks, 1);
}

TEST_F(BusyWaitIsrTest, UcIdleHighEarlyRisingEdgeBeforePostAttachReadDoesNotHang) {
  g_hw.busyLevel = LOW;
  // Rising edge lands right as attachInterrupt executes, before the post-attach level check.
  g_hw.onAttach = []() { g_hw.busyLevel = HIGH; };
  g_hw.onDelay = [](unsigned long) {
    if (g_hw.delayCalls >= 30) g_hw.busyLevel = HIGH;
  };

  bus.waitRefreshComplete("uc_early_edge");

  EXPECT_EQ(g_hw.attachCount, 1);
  EXPECT_EQ(g_hw.detachCount, 1);
  EXPECT_EQ(g_sem.blockingTakes, 0) << "early edge caught by post-attach level check must never block";
  EXPECT_EQ(g_beginHooks, 0);
  EXPECT_EQ(g_endHooks, 0);
}

TEST_F(BusyWaitIsrTest, UcIdleHighTimeoutCleanlyDetachesAndBalancesHooks) {
  g_hw.busyLevel = LOW;
  g_sem.onBlockingTake = [](TickType_t ticks) -> BaseType_t {
    g_hw.nowMs += ticks;
    return pdFALSE;
  };
  g_hw.onDelay = [](unsigned long) {
    if (g_hw.delayCalls >= 8005) g_hw.nowMs += 8005;
  };

  bus.waitRefreshComplete("uc_timeout");

  EXPECT_EQ(g_hw.attachCount, 1);
  EXPECT_EQ(g_hw.detachCount, 1);
  EXPECT_EQ(g_beginHooks, 1);
  EXPECT_EQ(g_endHooks, 1);
}

TEST(BusyWaitPowerPolicy, LowersAndRestoresAroundBusyWait) {
  busy_wait_power::State state;
  EXPECT_TRUE(state.shouldLowerOnBusyBegin(/*wifiActive=*/false));
  state.onBusyLowered();
  EXPECT_TRUE(state.isLowPower);
  EXPECT_TRUE(state.takeRestoreOnBusyEnd());
  state.onRestoredToNormal();
  EXPECT_FALSE(state.isLowPower);
  EXPECT_FALSE(state.takeRestoreOnBusyEnd());
}

TEST(BusyWaitPowerPolicy, DoesNotWakeAlreadyIdleDeviceOnPowerOffBusyWait) {
  busy_wait_power::State state;
  state.isLowPower = true;  // Already in idle low-power mode before powerOffPanel()
  EXPECT_FALSE(state.shouldLowerOnBusyBegin(/*wifiActive=*/false));
  EXPECT_FALSE(state.takeRestoreOnBusyEnd());
  EXPECT_TRUE(state.isLowPower);
}

TEST(BusyWaitPowerPolicy, MidBusyWakeByStorageSkipsRedundantRestore) {
  busy_wait_power::State state;
  ASSERT_TRUE(state.shouldLowerOnBusyBegin(/*wifiActive=*/false));
  state.onBusyLowered();
  // Storage or user input restored full speed mid-BUSY:
  state.onRestoredToNormal();
  EXPECT_FALSE(state.takeRestoreOnBusyEnd());
}

TEST(BusyWaitPowerPolicy, NeverLowersWhileWifiActive) {
  busy_wait_power::State state;
  EXPECT_FALSE(state.shouldLowerOnBusyBegin(/*wifiActive=*/true));
}

}  // namespace
