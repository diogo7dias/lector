#pragma once

#include <HalStorage.h>

#include "NearbyPositionReceive.h"
#include "activities/UiStatusActivity.h"
#include "network/EspNowLink.h"

// Receive-only: no reader state and no file transfer. All scan storage is bounded
// and belongs to the heap-allocated activity, not the small loop-task stack.
class NearbyPositionReceiveActivity final : public UiStatusActivity {
 public:
  NearbyPositionReceiveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("NearbyPositionReceive", renderer, mappedInput) {}
  void onEnter() override;
  void onExit() override;
  bool preventAutoSleep() override { return true; }
  bool skipLoopDelay() override { return state == State::Matching; }

 protected:
  StatusView statusView() const override;
  bool handleCustomInput() override;
  void onBackButton() override;
  void onConfirmButton() override;

 private:
  enum class State {
    Searching,
    Matching,
    Waiting,
    Applied,
    Kept,
    Same,
    NoMatch,
    Unavailable,
    Failed,
    TimedOut,
    RadioBusy
  };
  bool isComplete() const { return state == State::Applied || state == State::Kept || state == State::Same; }
  void setState(State next);
  void scanNext();
  bool tryBook(const std::string& path);
  bool applyPosition(const nearby_position::PacketView& packet);
  void closeScan();
  void reply();

  State state = State::Searching;
  EspNowLink link;
  EspNowLink::Received received;  // packet exceeds the 256-byte local-buffer budget
  nearby_position::CompactPosition peerPosition;
  std::array<uint8_t, nearby_position::MAC_BYTES> peerMac{};
  std::string documentHash;
  std::string matchedPath;
  std::string bookName;
  uint32_t startedAt = 0;
  uint32_t lastReplyAt = 0;
  bool replyAcknowledged = false;
  size_t recentIndex = 0;
  // ponytail: at most 12 nested folders and 4000 directories; refuse deeper
  // libraries. An on-card path index is the upgrade if measured scans need it.
  static constexpr size_t MAX_DEPTH = 12;
  std::array<HalFile, MAX_DEPTH + 1> directories;
  std::array<std::string, MAX_DEPTH + 1> paths;
  int depth = -1;
  size_t directoriesSeen = 0;
  char entryName[256]{};
};
