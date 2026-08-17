#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "NearbyPositionProtocol.h"

/**
 * The conversation half of Nearby Position Sync: what to send, what to expect
 * back, and when to give up.
 *
 * Like the codec beside it, this holds no radio, screen, or storage handles. The
 * activity feeds it decoded packets and the current millisecond clock, then
 * drains the actions it asks for. That keeps every timeout and transition
 * reachable from host tests, where the radio is not.
 *
 * Both readers run an identical session and both announce themselves; neither is
 * the server. Once they have each other's position, either reader can take the
 * other's place in the book, or push its own across. A position never moves
 * without the reader on the receiving device confirming it.
 */
namespace nearby_position {

/** How often an unpaired session announces itself. */
constexpr uint32_t HELLO_INTERVAL_MS = 750;
/** How often an unacknowledged POSITION or APPLY is sent again. */
constexpr uint32_t POSITION_RETRY_INTERVAL_MS = 750;
/** How long to look for a peer before giving up. */
constexpr uint32_t SEARCH_TIMEOUT_MS = 30000;
/** How long a paired peer may go silent before it counts as gone. */
constexpr uint32_t PEER_TIMEOUT_MS = 15000;

enum class SyncState : uint8_t {
  SEARCHING,        // Announcing, no peer yet
  COMPARING,        // Both positions known, waiting on the reader to choose
  APPLY_REQUESTED,  // The peer pushed its position; the reader must confirm
  SHARING,          // Our position was pushed, waiting for their acknowledgement
  SHARED,           // They accepted our position
  APPLIED,          // We took theirs
  BOOK_MISMATCH,    // A peer answered, but it has a different book open
  PEER_LOST,        // Paired, then silence
  TIMED_OUT,        // Nobody answered
};

enum class ActionKind : uint8_t {
  BROADCAST_HELLO,
  SEND_NAME,
  SEND_POSITION,
  SEND_ACK,
  SEND_APPLY,
};

struct Action {
  ActionKind kind = ActionKind::BROADCAST_HELLO;
  // Ignored for BROADCAST_HELLO, which goes to the broadcast address.
  std::array<uint8_t, MAC_BYTES> peerMac = {};
};

class SyncSession {
 public:
  /** Starts announcing this device's position. */
  void begin(const CompactPosition& localPosition, uint32_t nowMs);

  /**
   * This device's own MAC, so its broadcasts are not mistaken for a peer's when
   * the radio hears its own frames.
   */
  void setLocalMac(const std::array<uint8_t, MAC_BYTES>& mac) { localMac_ = mac; }

  /** Feeds in one decoded packet. Packets from other devices are ignored once paired. */
  void onPacket(const PacketView& packet, uint32_t nowMs);

  /**
   * Pops the next thing to transmit, or returns false when there is nothing to
   * do at `nowMs`. Call it in a loop until it returns false.
   */
  bool nextAction(uint32_t nowMs, Action& action);

  /** The reader accepted the peer's position. */
  void takePeerPosition(uint32_t nowMs);
  /** The reader chose to push this device's position to the peer instead. */
  void sharePosition(uint32_t nowMs);

  SyncState state() const { return state_; }
  bool hasPeer() const { return hasPeer_; }
  bool hasPeerPosition() const { return hasPeerPosition_; }
  const std::array<uint8_t, MAC_BYTES>& peerMac() const { return peerMac_; }
  const std::string& peerName() const { return peerName_; }
  const CompactPosition& peerPosition() const { return peerPosition_; }
  const CompactPosition& localPosition() const { return localPosition_; }

  /**
   * How the decoded packets were classified. Two readers stuck searching look the
   * same whether each hears only itself, hears a stranger it has already paired
   * away from, or hears nothing, and the counts separate those cases.
   */
  uint16_t packetsFromSelf() const { return packetsFromSelf_; }
  uint16_t packetsFromOthers() const { return packetsFromOthers_; }
  uint16_t packetsFromPeer() const { return packetsFromPeer_; }

  /** True when the peer is further into the book than this device. */
  bool peerIsFurtherAlong() const;
  /** True when both devices are on the same page of the same book. */
  bool positionsMatch() const;

 private:
  bool isFinished() const;
  bool sameBook(const CompactPosition& position) const;
  void pairWith(const PacketView& packet, uint32_t nowMs);

  SyncState state_ = SyncState::SEARCHING;
  CompactPosition localPosition_;
  CompactPosition peerPosition_;
  std::array<uint8_t, MAC_BYTES> localMac_ = {};
  std::array<uint8_t, MAC_BYTES> peerMac_ = {};
  std::string peerName_;

  bool hasPeer_ = false;
  bool hasPeerPosition_ = false;
  bool localPositionAcked_ = false;
  bool applyAcked_ = false;
  bool namePending_ = false;
  bool ackPending_ = false;

  uint32_t startedMs_ = 0;
  uint32_t lastHelloMs_ = 0;
  uint32_t lastPositionSendMs_ = 0;
  uint32_t lastApplySendMs_ = 0;
  uint32_t lastPeerPacketMs_ = 0;
  uint16_t packetsFromSelf_ = 0;
  uint16_t packetsFromOthers_ = 0;
  uint16_t packetsFromPeer_ = 0;
  bool helloSent_ = false;
  bool positionSent_ = false;
  bool applySent_ = false;
};

}  // namespace nearby_position
