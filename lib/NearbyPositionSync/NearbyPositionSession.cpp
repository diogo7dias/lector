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
  peerMac_ = packet.deviceMac;
  namePending_ = true;
  positionSent_ = false;
  localPositionAcked_ = false;
  lastPeerPacketMs_ = nowMs;
}

void SyncSession::onPacket(const PacketView& packet, const uint32_t nowMs) {
  if (isFinished()) return;
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
    if (packet.type != PacketType::HELLO) return;
    if (!sameBook(packet.position)) {
      state_ = SyncState::BOOK_MISMATCH;
      return;
    }
    pairWith(packet, nowMs);
    return;
  }

  lastPeerPacketMs_ = nowMs;
  switch (packet.type) {
    case PacketType::HELLO:
      break;
    case PacketType::NAME:
      peerName_ = packet.deviceName;
      break;
    case PacketType::POSITION:
      if (!sameBook(packet.position)) {
        state_ = SyncState::BOOK_MISMATCH;
        return;
      }
      peerPosition_ = packet.position;
      hasPeerPosition_ = true;
      ackPending_ = true;
      if (state_ == SyncState::SEARCHING) state_ = SyncState::COMPARING;
      break;
    case PacketType::APPLY:
      if (!sameBook(packet.position)) {
        state_ = SyncState::BOOK_MISMATCH;
        return;
      }
      peerPosition_ = packet.position;
      hasPeerPosition_ = true;
      ackPending_ = true;
      // Acknowledging tells the sender the request landed. It does not move this
      // device: only the reader here can do that.
      state_ = SyncState::APPLY_REQUESTED;
      break;
    case PacketType::ACK:
      if (state_ == SyncState::SHARING) {
        applyAcked_ = true;
        state_ = SyncState::SHARED;
      } else {
        localPositionAcked_ = true;
      }
      break;
  }
}

bool SyncSession::nextAction(const uint32_t nowMs, Action& action) {
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

  if (ackPending_) {
    ackPending_ = false;
    action = Action{ActionKind::SEND_ACK, peerMac_};
    return true;
  }

  if (namePending_) {
    namePending_ = false;
    action = Action{ActionKind::SEND_NAME, peerMac_};
    return true;
  }

  if (state_ == SyncState::SHARING && !applyAcked_ &&
      (!applySent_ || elapsed(nowMs, lastApplySendMs_, POSITION_RETRY_INTERVAL_MS))) {
    applySent_ = true;
    lastApplySendMs_ = nowMs;
    action = Action{ActionKind::SEND_APPLY, peerMac_};
    return true;
  }

  if (!localPositionAcked_ && (!positionSent_ || elapsed(nowMs, lastPositionSendMs_, POSITION_RETRY_INTERVAL_MS))) {
    positionSent_ = true;
    lastPositionSendMs_ = nowMs;
    action = Action{ActionKind::SEND_POSITION, peerMac_};
    return true;
  }

  return false;
}

void SyncSession::takePeerPosition(const uint32_t nowMs) {
  (void)nowMs;
  if (!hasPeerPosition_) return;
  state_ = SyncState::APPLIED;
}

void SyncSession::sharePosition(const uint32_t nowMs) {
  if (!hasPeer_) return;
  state_ = SyncState::SHARING;
  applyAcked_ = false;
  applySent_ = false;
  lastApplySendMs_ = nowMs;
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
