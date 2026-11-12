#include <gtest/gtest.h>

#include "WsIdleClients.h"

namespace {
struct FakeServer {
  bool connected[2] = {false, false};
  int closed = -1;
  bool clientIsConnected(const uint8_t i) const { return connected[i]; }
  void disconnect(const uint8_t i) { closed = i; }
};
constexpr uint32_t START = 1000;
}  // namespace

TEST(WsIdleClients, NewClientIsTimedFromFirstSight) {
  FakeServer ws;
  ws.connected[1] = true;
  uint32_t last[2] = {};
  ws_idle::closeIdleClients(ws, last, 2, START);
  EXPECT_EQ(0u, last[0]);
  EXPECT_EQ(START, last[1]);
  EXPECT_EQ(-1, ws.closed);
}

TEST(WsIdleClients, ClosesOnlyAfterTheTimeout) {
  FakeServer ws;
  ws.connected[0] = true;
  uint32_t last[2] = {START, 0};
  ws_idle::closeIdleClients(ws, last, 2, START + ws_idle::IDLE_TIMEOUT_MS);
  EXPECT_EQ(-1, ws.closed);
  ws_idle::closeIdleClients(ws, last, 2, START + ws_idle::IDLE_TIMEOUT_MS + 1);
  EXPECT_EQ(0, ws.closed);
}

TEST(WsIdleClients, SurvivesMillisWraparound) {
  FakeServer ws;
  ws.connected[0] = true;
  uint32_t last[2] = {UINT32_MAX - 10, 0};
  ws_idle::closeIdleClients(ws, last, 2, 100);
  EXPECT_EQ(-1, ws.closed);
}

TEST(WsIdleClients, LeftoverTimeOnAFreeSlotClosesNothing) {
  FakeServer ws;
  uint32_t last[2] = {START, 0};
  ws_idle::closeIdleClients(ws, last, 2, START + ws_idle::IDLE_TIMEOUT_MS * 2);
  EXPECT_EQ(-1, ws.closed);
}
