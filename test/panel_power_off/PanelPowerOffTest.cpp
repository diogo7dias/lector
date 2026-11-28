#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include "Ssd1677Driver.h"
#include "Uc8179Driver.h"

namespace freeink {

struct BusOp {
  enum Kind { Cmd, DataByte, DataBuf, WaitBusy, WaitRefresh, Flipped, Inverted } kind;
  uint8_t value;
};
static std::vector<BusOp> g_ops;

uint32_t EpdBus::_transferUs = 0;
uint32_t EpdBus::_busyUs = 0;
uint32_t EpdBus::_missedBusyAsserts = 0;
void EpdBus::reset(uint16_t) {}
void EpdBus::cmd(uint8_t c) { g_ops.push_back({BusOp::Cmd, c}); }
void EpdBus::data(uint8_t d) { g_ops.push_back({BusOp::DataByte, d}); }
void EpdBus::data(const uint8_t*, uint16_t) { g_ops.push_back({BusOp::DataBuf, 0}); }
void EpdBus::cmdData(uint8_t c, const uint8_t*, uint16_t) { g_ops.push_back({BusOp::Cmd, c}); }
void EpdBus::cmdData2(uint8_t c, uint8_t, uint8_t) { g_ops.push_back({BusOp::Cmd, c}); }
void EpdBus::beginTxn() {}
void EpdBus::endTxn() {}
void EpdBus::rawWriteBytes(const uint8_t*, uint16_t) {}
void EpdBus::waitBusy(const char*) { g_ops.push_back({BusOp::WaitBusy, 0}); }
void EpdBus::waitBusy(BusyPolarity, const char*) { g_ops.push_back({BusOp::WaitBusy, 0}); }
void EpdBus::waitRefreshComplete(const char*) { g_ops.push_back({BusOp::WaitRefresh, 0}); }
uint16_t EpdBus::waitIdleStable(unsigned long, unsigned long) { return 0; }
void EpdBus::sendPlaneFlipped(uint8_t c, const uint8_t*, uint16_t, uint16_t) { g_ops.push_back({BusOp::Flipped, c}); }
void EpdBus::sendPlaneFlippedInverted(uint8_t c, const uint8_t*, uint16_t, uint16_t) {
  g_ops.push_back({BusOp::Inverted, c});
}
void EpdBus::fillPlane(uint8_t c, uint8_t, uint16_t, uint16_t) { g_ops.push_back({BusOp::Cmd, c}); }

namespace {

bool hasCmdBytePair(const uint8_t cmd, const uint8_t data) {
  for (size_t i = 0; i + 1 < g_ops.size(); ++i) {
    if (g_ops[i].kind == BusOp::Cmd && g_ops[i].value == cmd && g_ops[i + 1].kind == BusOp::DataByte &&
        g_ops[i + 1].value == data) {
      return true;
    }
  }
  return false;
}

bool hasOp(const BusOp::Kind kind, const uint8_t value = 0) {
  return std::any_of(g_ops.begin(), g_ops.end(), [&](const BusOp& op) {
    return op.kind == kind && (kind >= BusOp::WaitBusy || op.value == value);
  });
}

class PanelPowerOffTest : public ::testing::Test {
 protected:
  EpdBus bus;
  std::vector<uint8_t> fb = std::vector<uint8_t>(100 * 480, 0xAA);
  void SetUp() override {
    g_ops.clear();
    g_fakeMillis = 0;
    g_fakeBusy = HIGH;
  }
};

TEST_F(PanelPowerOffTest, Ssd1677PowerOffDropsBoosterOnceAndKeepsNextTurnFast) {
  Ssd1677Driver driver;
  PanelDriver& base = driver;
  base.begin(bus);
  // First paint consumes _needsInitialFull (HALF 0xD7, self-powering off).
  base.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  // Second paint runs FAST (0xFC), which leaves the panel booster rails powered ON.
  base.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);

  g_ops.clear();
  base.powerOff(bus);
  // SSD1677 powerOffController: border 0x3C=0x80, CTRL2 0x22=0x03 (ANALOG_OFF|CLOCK_OFF), MASTER_ACTIVATION 0x20.
  EXPECT_TRUE(hasCmdBytePair(0x3C, 0x80));
  EXPECT_TRUE(hasCmdBytePair(0x22, 0x03));
  EXPECT_TRUE(hasOp(BusOp::Cmd, 0x20));
  EXPECT_TRUE(hasOp(BusOp::WaitBusy));

  // Repeat call while already powered off must emit zero bus traffic.
  g_ops.clear();
  base.powerOff(bus);
  EXPECT_TRUE(g_ops.empty());

  // First turn after idle power-off must stay FAST (0xFC self-enables clock+analog, no HALF promotion).
  g_ops.clear();
  base.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_TRUE(hasCmdBytePair(0x21, 0x00));  // CTRL1_NORMAL (differential, not BYPASS_RED)
  EXPECT_TRUE(hasCmdBytePair(0x22, 0xFC));  // FAST partial sequence (re-enables clock+analog)
  EXPECT_FALSE(hasCmdBytePair(0x22, 0xD7));
  EXPECT_FALSE(hasCmdBytePair(0x22, 0xF7));
}

TEST_F(PanelPowerOffTest, Uc8179PowerOffSendsPofOnceAndNextTurnRepowersFast) {
  Uc8179Driver driver;
  PanelDriver& base = driver;
  base.begin(bus);
  // Initial full paint seeds OLD plane and leaves _isScreenOn = true.
  base.display(bus, fb.data(), nullptr, RefreshMode::Full, false);

  g_ops.clear();
  base.powerOff(bus);
  EXPECT_TRUE(hasOp(BusOp::Cmd, 0x02));  // CMD_POWER_OFF (POF)
  EXPECT_TRUE(hasOp(BusOp::WaitBusy));

  // Repeat call while already powered off must emit zero bus traffic.
  g_ops.clear();
  base.powerOff(bus);
  EXPECT_TRUE(g_ops.empty());

  // Next FAST turn re-issues PON (0x04) and runs differential partial (PTIN 0x91), not forced full clear.
  g_ops.clear();
  base.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_TRUE(hasOp(BusOp::Cmd, 0x04));  // CMD_POWER_ON (PON)
  EXPECT_TRUE(hasOp(BusOp::Cmd, 0x91));  // CMD_PARTIAL_IN (PTIN)
  EXPECT_TRUE(hasOp(BusOp::Cmd, 0x12));  // CMD_DISPLAY_REFRESH (DRF)
}

}  // namespace
}  // namespace freeink
