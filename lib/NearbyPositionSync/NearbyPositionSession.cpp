#include "NearbyPositionSession.h"

#include <cstring>

namespace nearby_position {
namespace {

/** Wrap-safe elapsed time: millis() rolls over roughly every 49.7 days. */
bool elapsed(const uint32_t nowMs, const uint32_t sinceMs, const uint32_t intervalMs) {
  return static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
}

}  // namespace

void SyncSession::begin(const CompactPosition& localPosition, const uint32_t nowMs) {
  localPosition_ = localPosition;
  state_ = SyncState::SEARCHING;
  startedMs_ = nowMs;
  lastPeerPacketMs_ = nowMs;
  helloSent_ = false;
}

bool SyncSession::isFinished() const {
  switch (state_) {
    case SyncState::SHARED:
    case SyncState::APPLIED:
    case SyncState::BOOK_MISMATCH:
    case SyncState::PEER_LOST:
    case SyncState::TIMED_OUT:
      return true;
    default:
      return false;
  }
}

bool SyncSession::sameBook(const CompactPosition& position) const {
  return std::strncmp(position.documentHash.data(), localPosition_.documentHash.data(), DOCUMENT_HASH_BYTES) == 0;
}

void SyncSession::pairWith(const PacketView& packet, const uint32_t nowMs) {
  hasPeer_ = true;
  state_ = SyncState::EXCHANGING;
  peerMac_ = packet.deviceMac;
  namePending_ = true;
  positionSent_ = false;
  localPositionAcked_ = false;
  lastPeerPacketMs_ = nowMs;
}

void SyncSession::onPacket(const PacketView& packet, const uint32_t nowMs) {
  if (isFinished()) {
    // Keep answering retries during the outcome screen if the last ACK was lost.
    if ((state_ == SyncState::SHARED || state_ == SyncState::APPLIED) && packet.deviceMac == peerMac_ &&
        (packet.type == PacketType::POSITION || packet.type == PacketType::APPLY) && sameBook(packet.position)) {
      ackPending_ = true;
    }
    return;
  }
  // The radio can hear this device's own broadcast; syncing with itself would
  // pair the session against its own position and never progress.
  if (packet.deviceMac == localMac_) {
    packetsFromSelf_++;
    return;
  }
  // Once paired, a third reader in the room is not part of this conversation.
  if (hasPeer_ && packet.deviceMac != peerMac_) {
    packetsFromOthers_++;
    return;
  }
  packetsFromPeer_++;

  if (!hasPeer_) {
    // Any packet that names the book pairs, not only an announcement. The reader
    // that pairs first stops announcing itself and talks straight to this one, so
    // holding out for another HELLO leaves both sides searching until they give
    // up. NAME and ACK carry no book, and the sender repeats its position until
    // acknowledged, so ignoring those costs nothing.
    if (packet.type != PacketType::HELLO && packet.type != PacketType::POSITION &&
        packet.type != PacketType::APPLY) {
      return;
    }
    if (!sameBook(packet.position)) {
      state_ = SyncState::BOOK_MISMATCH;
      return;
    }
    pairWith(packet, nowMs);
    // Falls through, so the position this packet carried is not thrown away.
  }

  lastPeerPacketMs_ = nowMs;
  switch (packet.type) {
    case PacketType::HELLO:
      break;
    case PacketType::NAME:
      peerName_ = packet.deviceName;
      break;
    case PacketType::POSITION:
    case PacketType::APPLY:
      if (!sameBook(packet.position)) {
        state_ = SyncState::BOOK_MISMATCH;
        return;
      }
      peerPosition_ = packet.position;
      hasPeerPosition_ = true;
      ackPending_ = true;
      break;
    case PacketType::ACK:
      if (positionSent_) localPositionAcked_ = true;
      break;
  }
}

bool SyncSession::nextAction(const uint32_t nowMs, Action& action) {
  if (ackPending_) {
    ackPending_ = false;
    action = Action{ActionKind::SEND_ACK, peerMac_};
    return true;
  }
  if (isFinished()) return false;

  if (!hasPeer_) {
    if (elapsed(nowMs, startedMs_, SEARCH_TIMEOUT_MS)) {
      state_ = SyncState::TIMED_OUT;
      return false;
    }
    if (!helloSent_ || elapsed(nowMs, lastHelloMs_, HELLO_INTERVAL_MS)) {
      helloSent_ = true;
      lastHelloMs_ = nowMs;
      action = Action{ActionKind::BROADCAST_HELLO, {}};
      return true;
    }
    return false;
  }

  if (elapsed(nowMs, lastPeerPacketMs_, PEER_TIMEOUT_MS)) {
    state_ = SyncState::PEER_LOST;
    return false;
  }

  if (namePending_) {
    namePending_ = false;
    action = Action{ActionKind::SEND_NAME, peerMac_};
    return true;
  }

  if (!localPositionAcked_ && (!positionSent_ || elapsed(nowMs, lastPositionSendMs_, POSITION_RETRY_INTERVAL_MS))) {
    positionSent_ = true;
    lastPositionSendMs_ = nowMs;
    action = Action{ActionKind::SEND_POSITION, peerMac_};
    return true;
  }

  if (hasPeerPosition_ && localPositionAcked_) {
    state_ = resolution() == Resolution::TakePeer ? SyncState::APPLIED : SyncState::SHARED;
  }

  return false;
}

bool SyncSession::peerIsFurtherAlong() const {
  if (!hasPeerPosition_) return false;
  return peerPosition_.percentageQ > localPosition_.percentageQ;
}

bool SyncSession::positionsMatch() const {
  if (!hasPeerPosition_) return false;
  return peerPosition_.percentageQ == localPosition_.percentageQ &&
         peerPosition_.spineIndex == localPosition_.spineIndex && peerPosition_.pageNumber == localPosition_.pageNumber;
}

}  // namespace nearby_position
