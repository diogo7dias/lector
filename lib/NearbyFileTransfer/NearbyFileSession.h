#pragma once

#include <NearbyTransfer.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

/**
 * The choreography of sending one file between two readers: find each other,
 * offer, accept, push the bytes, check they arrived intact.
 *
 * The transport underneath is the SDK's freeink::nearby library, which owns the
 * packet format, the chunk sequencing, and the running CRC32. This layer owns
 * only the conversation: who speaks when, what a silence means, and what the
 * reader is asked to confirm.
 *
 * It performs no file access and touches no radio. The activity reads and writes
 * the card, and sends what nextAction() asks for, so every path here is
 * reachable from host tests.
 */
namespace nearby_file {

/** How often a sender announces that it is looking for somewhere to send. */
constexpr uint32_t DISCOVER_INTERVAL_MS = 700;
/** How often an unanswered offer is repeated. */
constexpr uint32_t OFFER_RETRY_INTERVAL_MS = 900;
/** How long to wait for an acknowledgement before sending the chunk again. */
constexpr uint32_t CHUNK_RETRY_INTERVAL_MS = 600;
/** How long the other reader may go silent before the transfer is abandoned. */
constexpr uint32_t PEER_SILENCE_TIMEOUT_MS = 12000;

enum class TransferState : uint8_t {
  DISCOVERING,   // Sender: looking for readers
  PEERS_FOUND,   // Sender: at least one reader answered, waiting on a choice
  OFFER_SENT,    // Sender: offer out, waiting for accept or reject
  LISTENING,     // Receiver: waiting to be found
  OFFER_PROMPT,  // Receiver: an offer arrived, waiting on the reader here
  TRANSFERRING,  // Bytes moving
  VERIFYING,     // Sender: all bytes sent, waiting for the checksum verdict
  DONE,
  REJECTED,
  CANCELLED,
  FAILED,
};

enum class TransferActionKind : uint8_t {
  BROADCAST_DISCOVER,
  SEND_ADVERTISE,
  SEND_OFFER,
  SEND_ACCEPT,
  SEND_REJECT,
  SEND_DATA,
  SEND_ACK,
  SEND_COMPLETE,
  SEND_RESULT,
  SEND_CANCEL,
};

struct TransferAction {
  TransferActionKind kind = TransferActionKind::BROADCAST_DISCOVER;
  std::array<uint8_t, freeink::nearby::MAC_BYTES> peerMac = {};
  // SEND_DATA: which chunk, and the slice of the file it carries. The activity
  // reads exactly these bytes off the card and puts them on the air.
  uint32_t sequence = 0;
  uint64_t offset = 0;
  uint16_t length = 0;
  // SEND_RESULT: whether the received file matched the sender's checksum.
  bool success = false;
};

enum class TransferEventKind : uint8_t {
  DISCOVER,
  ADVERTISE,
  OFFER,
  ACCEPT,
  REJECT,
  ACK,
  COMPLETE,
  RESULT,
  CANCEL,
};

/** One decoded packet, with only the fields its kind carries filled in. */
struct TransferEvent {
  TransferEventKind kind = TransferEventKind::DISCOVER;
  std::array<uint8_t, freeink::nearby::MAC_BYTES> sourceMac = {};
  std::string deviceName;
  std::string fileName;
  uint64_t fileSize = 0;
  uint32_t crc32 = 0;
  bool success = false;
};

struct DiscoveredPeer {
  std::array<uint8_t, freeink::nearby::MAC_BYTES> mac = {};
  std::string name;
};

class TransferSession {
 public:
  /** Bytes per chunk. Fixed, so both ends agree without negotiating. */
  static constexpr uint16_t chunkBytes() { return freeink::nearby::V2_CHUNK_BYTES; }

  /** Starts looking for a reader to send `fileName` to. */
  void beginSend(const std::string& fileName, uint64_t fileSize, uint32_t nowMs);
  /** Starts waiting to be found by a sender. */
  void beginReceive(uint32_t nowMs);

  void onEvent(const TransferEvent& incoming, uint32_t nowMs);
  bool nextAction(uint32_t nowMs, TransferAction& action);

  /** Sender: the reader picked which device to send to. */
  void choosePeer(const std::array<uint8_t, freeink::nearby::MAC_BYTES>& mac, uint32_t nowMs);
  /** Sender: the activity put `length` bytes of the current chunk on the air. */
  void onChunkSent(uint16_t length, uint32_t nowMs);

  /** Receiver: the reader accepted, and the activity resolved where it goes. */
  void acceptOffer(const std::string& destinationPath, uint32_t nowMs);
  /** Receiver: the reader declined. */
  void rejectOffer(uint32_t nowMs);
  /**
   * Receiver: takes one arriving chunk. True when it is the chunk that was due
   * and the activity should write these bytes; false when it is a repeat or out
   * of order, in which case nothing is written.
   */
  bool acceptChunk(uint32_t sequence, const uint8_t* data, size_t length, uint32_t nowMs);

  /** Either side: stop, and tell the other reader why. */
  void cancel(uint32_t nowMs);

  TransferState state() const { return state_; }
  size_t peerCount() const { return peers_.size(); }
  const DiscoveredPeer& peerAt(const size_t index) const { return peers_[index]; }
  const std::string& peerName() const { return peerName_; }
  const std::string& offeredName() const { return offeredName_; }
  uint64_t offeredSize() const { return session_.totalBytes(); }
  const std::string& destinationPath() const { return destinationPath_; }
  uint64_t transferredBytes() const { return session_.transferredBytes(); }
  uint32_t crc32() const { return session_.crc32(); }
  int progressPercent() const;
  /**
   * True when a partly written file is on the card and must be deleted: the
   * transfer was cancelled, or the bytes did not survive the trip.
   */
  bool shouldDiscardPartialFile() const;

 private:
  bool isFinished() const;
  bool isFromPeer(const std::array<uint8_t, freeink::nearby::MAC_BYTES>& mac) const;
  void rememberPeer(const TransferEvent& incoming);
  void finish(TransferState state);

  TransferState state_ = TransferState::LISTENING;
  freeink::nearby::ReliableTransferSession session_;
  std::vector<DiscoveredPeer> peers_;

  std::array<uint8_t, freeink::nearby::MAC_BYTES> peerMac_ = {};
  bool hasPeer_ = false;
  std::string peerName_;
  std::string fileName_;
  std::string offeredName_;
  std::string destinationPath_;

  // Queued one-shot sends, drained by nextAction in the order they were raised.
  bool advertisePending_ = false;
  bool acceptPending_ = false;
  bool rejectPending_ = false;
  bool ackPending_ = false;
  bool completePending_ = false;
  bool resultPending_ = false;
  bool cancelPending_ = false;
  bool resultSuccess_ = false;

  // Sender chunk bookkeeping: the chunk in flight and whether it is still
  // waiting to be acknowledged.
  uint64_t chunkOffset_ = 0;
  uint16_t chunkLength_ = 0;
  // Handed to the activity but not yet reported as sent. Without this the same
  // chunk would be offered on every call, since nothing else changes until the
  // activity has read the card and put the bytes on the air.
  bool chunkOffered_ = false;
  bool chunkInFlight_ = false;
  bool discoverSent_ = false;
  bool offerSent_ = false;
  bool receivedIntact_ = false;

  uint32_t lastDiscoverMs_ = 0;
  uint32_t lastOfferMs_ = 0;
  uint32_t lastChunkSendMs_ = 0;
  uint32_t lastPeerPacketMs_ = 0;
};

}  // namespace nearby_file
