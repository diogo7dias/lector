// X3 ghosting fixes, pinned at the bus: which waveform bank a refresh loads and what it writes
// into the controller's old/new planes. QEMU does not model waveforms, so the only place these
// rules can be checked without a panel is the traffic the drivers emit.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "../lut/Uc8253X3Luts.h"
#include "../lut/Uc8279X3Luts.h"
#include "Uc8253X3Driver.h"
#include "Uc8279Driver.h"

namespace freeink {

// Recording stand-in for the real EpdBus (EpdBus.cpp is not linked).
struct Ev {
  enum Kind { Cmd, CmdData, Flipped, Inverted, Fill } kind;
  uint8_t cmd;
  const uint8_t* ptr;  // CmdData: the table; Flipped/Inverted: the plane
  uint8_t fill;
};
static std::vector<Ev> g_ev;

uint32_t EpdBus::_transferUs = 0;
uint32_t EpdBus::_busyUs = 0;
uint32_t EpdBus::_missedBusyAsserts = 0;
void EpdBus::reset(uint16_t) {}
void EpdBus::cmd(uint8_t c) { g_ev.push_back({Ev::Cmd, c, nullptr, 0}); }
void EpdBus::data(uint8_t) {}
void EpdBus::data(const uint8_t*, uint16_t) {}
void EpdBus::cmdData(uint8_t c, const uint8_t* d, uint16_t) { g_ev.push_back({Ev::CmdData, c, d, 0}); }
void EpdBus::cmdData2(uint8_t, uint8_t, uint8_t) {}
void EpdBus::beginTxn() {}
void EpdBus::endTxn() {}
void EpdBus::rawWriteBytes(const uint8_t*, uint16_t) {}
void EpdBus::waitBusy(const char*) {}
void EpdBus::waitBusy(BusyPolarity, const char*) {}
void EpdBus::waitRefreshComplete(const char*) {}
uint16_t EpdBus::waitIdleStable(unsigned long, unsigned long) { return 0; }
void EpdBus::sendPlaneFlipped(uint8_t c, const uint8_t* p, uint16_t, uint16_t) {
  g_ev.push_back({Ev::Flipped, c, p, 0});
}
void EpdBus::sendPlaneFlippedInverted(uint8_t c, const uint8_t* p, uint16_t, uint16_t) {
  g_ev.push_back({Ev::Inverted, c, p, 0});
}
void EpdBus::fillPlane(uint8_t c, uint8_t f, uint16_t, uint16_t) { g_ev.push_back({Ev::Fill, c, nullptr, f}); }

namespace {
constexpr uint8_t DTM1 = 0x10, DRF = 0x12, LUT_WW = 0x21;

class X3PanelDriverTest : public ::testing::Test {
 protected:
  EpdBus bus;
  std::vector<uint8_t> fb = std::vector<uint8_t>(99 * 528, 0xA5);
  void SetUp() override { g_ev.clear(); }

  // The WW table the most recent refresh loaded before its DRF.
  const uint8_t* wwLoaded() const {
    const uint8_t* ww = nullptr;
    for (const Ev& e : g_ev) {
      if (e.kind == Ev::CmdData && e.cmd == LUT_WW) ww = e.ptr;
      if (e.kind == Ev::Cmd && e.cmd == DRF) break;
    }
    return ww;
  }
  // First plane event on `ramCmd` before the first DRF, if any.
  const Ev* planeBeforeRefresh(uint8_t ramCmd) const {
    for (const Ev& e : g_ev) {
      if (e.kind == Ev::Cmd && e.cmd == DRF) return nullptr;
      if ((e.kind == Ev::Flipped || e.kind == Ev::Inverted || e.kind == Ev::Fill) && e.cmd == ramCmd) return &e;
    }
    return nullptr;
  }
};

// ---- UC8253 ------------------------------------------------------------------------------

TEST_F(X3PanelDriverTest, Uc8253FirstPaintAfterBeginIsTheHalfScrub) {
  Uc8253X3Driver d;
  d.begin(bus);
  d.skipInitialResync();  // the seamless wake: panel holds a face the controller knows nothing about
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_EQ(wwLoaded(), lut_x3_ww_half);
}

TEST_F(X3PanelDriverTest, Uc8253PaintAfterIdlePowerOffIsStillAFastDifferential) {
  Uc8253X3Driver d;
  d.begin(bus);
  d.skipInitialResync();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);  // consumes the first-paint promotion
  d.powerOff(bus);                                               // main.cpp does this 2 s after the last input
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_EQ(wwLoaded(), lut_x3_ww_fast) << "every page turn after idle used to run the half scrub";
}

TEST_F(X3PanelDriverTest, Uc8253FullSyncWritesTheTargetIntoTheOldPlane) {
  Uc8253X3Driver d;
  d.begin(bus);
  d.skipInitialResync();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);  // consumes the first-paint promotion
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Full, false);
  EXPECT_EQ(wwLoaded(), lut_x3_ww_full);
  const Ev* old = planeBeforeRefresh(DTM1);
  ASSERT_NE(old, nullptr);
  // A white-filled old plane made every black target a WB cell, which never visits the white
  // rail. Old == new makes every pixel an unchanged cell, and those are the two that flash.
  EXPECT_EQ(old->kind, Ev::Flipped);
  EXPECT_EQ(old->ptr, fb.data());
}

// ---- UC8279 ------------------------------------------------------------------------------

// An Inverted old plane is how the driver re-drives every pixel toward its target.
static bool hasInvertedOldPlane() {
  return std::any_of(g_ev.begin(), g_ev.end(), [](const Ev& e) { return e.kind == Ev::Inverted && e.cmd == DTM1; });
}

TEST_F(X3PanelDriverTest, Uc8279SeamlessWakeDrivesEveryPixelOnItsFirstPaint) {
  Uc8279Driver d;
  d.begin(bus);
  d.skipInitialResync();
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_TRUE(hasInvertedOldPlane()) << "a plain DU diffs against a DTM1 nobody wrote and leaves the old face standing";
}

TEST_F(X3PanelDriverTest, Uc8279DriveAllIsOneShot) {
  Uc8279Driver d;
  d.begin(bus);
  d.skipInitialResync();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_FALSE(hasInvertedOldPlane()) << "the second paint is an ordinary differential: the old plane is already right";
}

TEST_F(X3PanelDriverTest, Uc8279HonoursTheWakeDriveAllRequest) {
  Uc8279Driver d;
  d.begin(bus);
  d.skipInitialResync();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  d.requestDriveAllNextFast();
  g_ev.clear();
  d.display(bus, fb.data(), nullptr, RefreshMode::Fast, false);
  EXPECT_TRUE(hasInvertedOldPlane());
}

}  // namespace
}  // namespace freeink
