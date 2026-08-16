#include "NearbyFileSession.h"

#include <algorithm>

namespace nearby_file {
namespace {

using freeink::nearby::ReliableTransferSession;

/** Wrap-safe elapsed time: millis() rolls over roughly every 49.7 days. */
bool elapsed(const uint32_t nowMs, const uint32_t sinceMs, const uint32_t intervalMs) {
  return static_cast<uint32_t>(nowMs - sinceMs) >= intervalMs;
}

}  // namespace

void TransferSession::beginSend(const std::string& fileName, const uint64_t fileSize, const uint32_t nowMs) {
  fileName_ = fileName;
  session_.begin(ReliableTransferSession::Role::Sender, nowMs, fileSize, chunkBytes());
  state_ = TransferState::DISCOVERING;
  discoverSent_ = false;
  lastPeerPacketMs_ = nowMs;
}

void TransferSession::beginReceive(const uint32_t nowMs) {
  state_ = TransferState::LISTENING;
  lastPeerPacketMs_ = nowMs;
}

bool TransferSession::isFinished() const {
  switch (state_) {
    case TransferState::DONE:
    case TransferState::REJECTED:
    case TransferState::CANCELLED:
    case TransferState::FAILED:
      return true;
    default:
      return false;
  }
}

bool TransferSession::isFromPeer(const std::array<uint8_t, freeink::nearby::MAC_BYTES>& mac) const {
  return hasPeer_ && mac == peerMac_;
}

void TransferSession::rememberPeer(const TransferEvent& incoming) {
  for (const DiscoveredPeer& peer : peers_) {
    if (peer.mac == incoming.sourceMac) return;
  }
  peers_.push_back(DiscoveredPeer{incoming.sourceMac, incoming.deviceName});
}

void TransferSession::finish(const TransferState state) { state_ = state; }

void TransferSession::onEvent(const TransferEvent& incoming, const uint32_t nowMs) {
  if (isFinished()) return;

  // Once a pair is talking, a third reader in the room cannot steer the
  // transfer: an ACK or a CANCEL from anyone else is not ours to act on.
  const bool pairedState = state_ == TransferState::OFFER_SENT || state_ == TransferState::TRANSFERRING ||
                           state_ == TransferState::VERIFYING || state_ == TransferState::OFFER_PROMPT;
  if (pairedState && hasPeer_ && !isFromPeer(incoming.sourceMac)) return;

  if (hasPeer_ && isFromPeer(incoming.sourceMac)) lastPeerPacketMs_ = nowMs;

  switch (incoming.kind) {
    case TransferEventKind::DISCOVER:
      // Only a reader that is waiting to receive answers, and it answers the
      // sender directly rather than broadcasting.
      if (state_ == TransferState::LISTENING) {
        peerMac_ = incoming.sourceMac;
        hasPeer_ = true;
        peerName_ = incoming.deviceName;
        advertisePending_ = true;
        lastPeerPacketMs_ = nowMs;
      }
      break;

    case TransferEventKind::ADVERTISE:
      if (state_ == TransferState::DISCOVERING || state_ == TransferState::PEERS_FOUND) {
        rememberPeer(incoming);
        state_ = TransferState::PEERS_FOUND;
      }
      break;

    case TransferEventKind::OFFER:
      if (state_ == TransferState::LISTENING || state_ == TransferState::OFFER_PROMPT) {
        peerMac_ = incoming.sourceMac;
        hasPeer_ = true;
        if (!incoming.deviceName.empty()) peerName_ = incoming.deviceName;
        offeredName_ = incoming.fileName;
        session_.begin(ReliableTransferSession::Role::Receiver, nowMs, incoming.fileSize, chunkBytes());
        state_ = TransferState::OFFER_PROMPT;
        lastPeerPacketMs_ = nowMs;
      }
      break;

    case TransferEventKind::ACCEPT:
      if (state_ == TransferState::OFFER_SENT) state_ = TransferState::TRANSFERRING;
      break;

    case TransferEventKind::REJECT:
      if (state_ == TransferState::OFFER_SENT) finish(TransferState::REJECTED);
      break;

    case TransferEventKind::ACK:
      if (state_ == TransferState::TRANSFERRING && chunkInFlight_) {
        // The SDK session only advances on the acknowledgement it is waiting
        // for, so a duplicate or stale ACK cannot skip a chunk.
        if (session_.acceptAcknowledgement(session_.nextSequence() + 1)) {
          session_.advanceSentBytes(chunkLength_);
          chunkOffset_ += chunkLength_;
          chunkInFlight_ = false;
        }
      }
      break;

    case TransferEventKind::COMPLETE:
      if (state_ == TransferState::TRANSFERRING) {
        receivedIntact_ = incoming.crc32 == session_.crc32() && session_.transferredBytes() == session_.totalBytes();
        resultPending_ = true;
        resultSuccess_ = receivedIntact_;
        finish(receivedIntact_ ? TransferState::DONE : TransferState::FAILED);
      }
      break;

    case TransferEventKind::RESULT:
      if (state_ == TransferState::VERIFYING) finish(incoming.success ? TransferState::DONE : TransferState::FAILED);
      break;

    case TransferEventKind::CANCEL:
      if (!isFinished()) finish(TransferState::CANCELLED);
      break;
  }
}

void TransferSession::choosePeer(const std::array<uint8_t, freeink::nearby::MAC_BYTES>& mac, const uint32_t nowMs) {
  if (state_ != TransferState::PEERS_FOUND && state_ != TransferState::DISCOVERING) return;

  peerMac_ = mac;
  hasPeer_ = true;
  for (const DiscoveredPeer& peer : peers_) {
    if (peer.mac == mac) peerName_ = peer.name;
  }
  state_ = TransferState::OFFER_SENT;
  offerSent_ = false;
  lastPeerPacketMs_ = nowMs;
}

void TransferSession::acceptOffer(const std::string& destinationPath, const uint32_t nowMs) {
  if (state_ != TransferState::OFFER_PROMPT) return;
  destinationPath_ = destinationPath;
  acceptPending_ = true;
  state_ = TransferState::TRANSFERRING;
  lastPeerPacketMs_ = nowMs;
}

void TransferSession::rejectOffer(const uint32_t nowMs) {
  if (state_ != TransferState::OFFER_PROMPT) return;
  (void)nowMs;
  rejectPending_ = true;
  finish(TransferState::REJECTED);
}

bool TransferSession::acceptChunk(const uint32_t sequence, const uint8_t* data, const size_t length,
                                  const uint32_t nowMs) {
  if (state_ != TransferState::TRANSFERRING) return false;
  lastPeerPacketMs_ = nowMs;

  if (!session_.acceptReceivedChunk(sequence, length)) {
    // Either a repeat of a chunk already written or one from further ahead.
    // Acknowledging the repeat keeps the sender moving; a gap is left
    // unacknowledged so the sender resends what is actually due.
    if (sequence < session_.nextSequence()) ackPending_ = true;
    return false;
  }

  session_.includeBytes(data, length);
  ackPending_ = true;
  return true;
}

void TransferSession::onChunkSent(const uint16_t length, const uint32_t nowMs) {
  if (state_ != TransferState::TRANSFERRING) return;
  chunkLength_ = length;
  chunkOffered_ = false;
  chunkInFlight_ = true;
  lastChunkSendMs_ = nowMs;
}

void TransferSession::cancel(const uint32_t nowMs) {
  (void)nowMs;
  if (isFinished()) return;
  if (hasPeer_) cancelPending_ = true;
  finish(TransferState::CANCELLED);
}

bool TransferSession::nextAction(const uint32_t nowMs, TransferAction& action) {
  // A cancel still has to reach the other reader, so it is drained even though
  // the session is finished by then.
  if (cancelPending_) {
    cancelPending_ = false;
    action = TransferAction{TransferActionKind::SEND_CANCEL, peerMac_, 0, 0, 0, false};
    return true;
  }
  if (rejectPending_) {
    rejectPending_ = false;
    action = TransferAction{TransferActionKind::SEND_REJECT, peerMac_, 0, 0, 0, false};
    return true;
  }
  if (resultPending_) {
    resultPending_ = false;
    action = TransferAction{TransferActionKind::SEND_RESULT, peerMac_, 0, 0, 0, resultSuccess_};
    return true;
  }
  if (isFinished()) return false;

  if (advertisePending_) {
    advertisePending_ = false;
    action = TransferAction{TransferActionKind::SEND_ADVERTISE, peerMac_, 0, 0, 0, false};
    return true;
  }
  if (acceptPending_) {
    acceptPending_ = false;
    action = TransferAction{TransferActionKind::SEND_ACCEPT, peerMac_, 0, 0, 0, false};
    return true;
  }
  if (ackPending_) {
    ackPending_ = false;
    action = TransferAction{TransferActionKind::SEND_ACK, peerMac_, session_.nextSequence(), 0, 0, false};
    return true;
  }
  if (completePending_) {
    completePending_ = false;
    action = TransferAction{TransferActionKind::SEND_COMPLETE, peerMac_, 0, 0, 0, false};
    return true;
  }

  switch (state_) {
    case TransferState::DISCOVERING:
    case TransferState::PEERS_FOUND:
      if (!discoverSent_ || elapsed(nowMs, lastDiscoverMs_, DISCOVER_INTERVAL_MS)) {
        discoverSent_ = true;
        lastDiscoverMs_ = nowMs;
        action = TransferAction{TransferActionKind::BROADCAST_DISCOVER, {}, 0, 0, 0, false};
        return true;
      }
      return false;

    case TransferState::OFFER_SENT:
      if (!offerSent_ || elapsed(nowMs, lastOfferMs_, OFFER_RETRY_INTERVAL_MS)) {
        offerSent_ = true;
        lastOfferMs_ = nowMs;
        action = TransferAction{TransferActionKind::SEND_OFFER, peerMac_, 0, 0, 0, false};
        return true;
      }
      if (elapsed(nowMs, lastPeerPacketMs_, PEER_SILENCE_TIMEOUT_MS)) finish(TransferState::FAILED);
      return false;

    case TransferState::TRANSFERRING: {
      // The receiver has nothing to send here: its acknowledgements are queued
      // by acceptChunk and drained above.
      if (session_.role() != ReliableTransferSession::Role::Sender) {
        if (elapsed(nowMs, lastPeerPacketMs_, PEER_SILENCE_TIMEOUT_MS)) finish(TransferState::FAILED);
        return false;
      }

      // Already handed over; the activity is reading the card. Offering it again
      // would put the same bytes on the air twice.
      if (chunkOffered_) return false;

      if (chunkInFlight_) {
        if (elapsed(nowMs, lastPeerPacketMs_, PEER_SILENCE_TIMEOUT_MS)) {
          finish(TransferState::FAILED);
          return false;
        }
        if (elapsed(nowMs, lastChunkSendMs_, CHUNK_RETRY_INTERVAL_MS)) {
          // Resend exactly the chunk that is outstanding. Skipping ahead would
          // tear a hole in the file on the other end.
          lastChunkSendMs_ = nowMs;
          chunkOffered_ = true;
          action = TransferAction{
              TransferActionKind::SEND_DATA, peerMac_, session_.nextSequence(), chunkOffset_, chunkLength_, false};
          return true;
        }
        return false;
      }

      if (chunkOffset_ >= session_.totalBytes()) {
        completePending_ = true;
        state_ = TransferState::VERIFYING;
        return nextAction(nowMs, action);
      }

      const uint64_t remaining = session_.totalBytes() - chunkOffset_;
      const uint16_t length = static_cast<uint16_t>(std::min<uint64_t>(remaining, chunkBytes()));
      chunkOffered_ = true;
      action =
          TransferAction{TransferActionKind::SEND_DATA, peerMac_, session_.nextSequence(), chunkOffset_, length, false};
      return true;
    }

    case TransferState::VERIFYING:
      if (elapsed(nowMs, lastPeerPacketMs_, PEER_SILENCE_TIMEOUT_MS)) finish(TransferState::FAILED);
      return false;

    case TransferState::OFFER_PROMPT:
    case TransferState::LISTENING:
    default:
      return false;
  }
}

int TransferSession::progressPercent() const {
  if (session_.totalBytes() == 0) return 0;
  return static_cast<int>((session_.transferredBytes() * 100ULL) / session_.totalBytes());
}

bool TransferSession::shouldDiscardPartialFile() const {
  if (session_.role() != ReliableTransferSession::Role::Receiver) return false;
  return state_ == TransferState::CANCELLED || state_ == TransferState::FAILED;
}

}  // namespace nearby_file
