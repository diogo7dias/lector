#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lib/NearbyFileTransfer/NearbyFileSession.h"

namespace {

using namespace nearby_file;

constexpr std::array<uint8_t, 6> PEER_MAC = {0xaa, 0xbb, 0xcc, 0x01, 0x02, 0x03};
constexpr std::array<uint8_t, 6> OTHER_MAC = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};

std::vector<TransferAction> drain(TransferSession& session, const uint32_t nowMs) {
  std::vector<TransferAction> actions;
  TransferAction action;
  while (session.nextAction(nowMs, action)) actions.push_back(action);
  return actions;
}

bool contains(const std::vector<TransferAction>& actions, const TransferActionKind kind) {
  for (const TransferAction& action : actions) {
    if (action.kind == kind) return true;
  }
  return false;
}

TransferEvent event(const TransferEventKind kind, const std::array<uint8_t, 6>& mac = PEER_MAC) {
  TransferEvent incoming;
  incoming.kind = kind;
  incoming.sourceMac = mac;
  return incoming;
}

/** A sender that has had its offer accepted and is ready to push bytes. */
TransferSession acceptedSender(const uint64_t fileSize, uint32_t& nowMs) {
  TransferSession session;
  session.beginSend("Book.epub", fileSize, nowMs);
  drain(session, nowMs);

  TransferEvent advertise = event(TransferEventKind::ADVERTISE);
  advertise.deviceName = "Lector-4B2C";
  session.onEvent(advertise, nowMs);
  session.choosePeer(PEER_MAC, nowMs);
  drain(session, nowMs);

  // Deliberately not drained: the first chunk is what the caller inspects.
  session.onEvent(event(TransferEventKind::ACCEPT), nowMs);
  return session;
}

}  // namespace

TEST(NearbyFileSession, SenderLooksForReadersBeforeOfferingAnything) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginSend("Book.epub", 4096, now);

  EXPECT_EQ(session.state(), TransferState::DISCOVERING);
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::BROADCAST_DISCOVER));
  EXPECT_TRUE(drain(session, now + 1).empty());
  EXPECT_TRUE(contains(drain(session, now + DISCOVER_INTERVAL_MS), TransferActionKind::BROADCAST_DISCOVER));
}

TEST(NearbyFileSession, SenderCollectsEveryReaderThatAnswers) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginSend("Book.epub", 4096, now);
  drain(session, now);

  TransferEvent first = event(TransferEventKind::ADVERTISE, PEER_MAC);
  first.deviceName = "Lector-4B2C";
  session.onEvent(first, now);

  TransferEvent second = event(TransferEventKind::ADVERTISE, OTHER_MAC);
  second.deviceName = "Lector-9F01";
  session.onEvent(second, now);

  // The same reader answering twice is still one entry in the list.
  session.onEvent(first, now + 10);

  ASSERT_EQ(session.peerCount(), 2u);
  EXPECT_EQ(session.peerAt(0).name, "Lector-4B2C");
  EXPECT_EQ(session.peerAt(1).name, "Lector-9F01");
  EXPECT_EQ(session.state(), TransferState::PEERS_FOUND);
}

TEST(NearbyFileSession, ChoosingAReaderSendsTheOffer) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginSend("Book.epub", 4096, now);
  drain(session, now);
  session.onEvent(event(TransferEventKind::ADVERTISE), now);

  session.choosePeer(PEER_MAC, now);
  const std::vector<TransferAction> actions = drain(session, now);
  ASSERT_TRUE(contains(actions, TransferActionKind::SEND_OFFER));
  EXPECT_EQ(session.state(), TransferState::OFFER_SENT);

  // An unanswered offer is sent again rather than left hanging.
  now += OFFER_RETRY_INTERVAL_MS;
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_OFFER));
}

TEST(NearbyFileSession, SenderStopsWhenTheOfferIsDeclined) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginSend("Book.epub", 4096, now);
  drain(session, now);
  session.onEvent(event(TransferEventKind::ADVERTISE), now);
  session.choosePeer(PEER_MAC, now);
  drain(session, now);

  session.onEvent(event(TransferEventKind::REJECT), now);
  EXPECT_EQ(session.state(), TransferState::REJECTED);
  EXPECT_TRUE(drain(session, now + OFFER_RETRY_INTERVAL_MS * 4).empty());
}

TEST(NearbyFileSession, SenderWalksThroughTheFileOneChunkAtATime) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk * 2 + 10, now);

  ASSERT_EQ(session.state(), TransferState::TRANSFERRING);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.kind, TransferActionKind::SEND_DATA);
  EXPECT_EQ(action.sequence, 0u);
  EXPECT_EQ(action.offset, 0u);
  EXPECT_EQ(action.length, chunk);

  // Nothing more goes out until the reader acknowledges what was already sent,
  // so a slow card on the far end cannot be flooded.
  session.onChunkSent(chunk, now);
  EXPECT_FALSE(contains(drain(session, now), TransferActionKind::SEND_DATA));

  session.onEvent(event(TransferEventKind::ACK), now);
  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.sequence, 1u);
  EXPECT_EQ(action.offset, chunk);

  session.onChunkSent(chunk, now);
  session.onEvent(event(TransferEventKind::ACK), now);

  // The last chunk is only what is left, never a full one past the end.
  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.sequence, 2u);
  EXPECT_EQ(action.length, 10u);
}

TEST(NearbyFileSession, SenderRepeatsAChunkThatIsNeverAcknowledged) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk * 4, now);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(chunk, now);

  now += CHUNK_RETRY_INTERVAL_MS;
  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.kind, TransferActionKind::SEND_DATA);
  EXPECT_EQ(action.sequence, 0u) << "a retry must resend the same chunk, not skip ahead";
  EXPECT_EQ(action.offset, 0u);
}

TEST(NearbyFileSession, SenderGivesUpOnAReaderThatStopsAnswering) {
  uint32_t now = 1000;
  TransferSession session = acceptedSender(TransferSession::chunkBytes() * 4, now);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(action.length, now);

  drain(session, now + PEER_SILENCE_TIMEOUT_MS);
  EXPECT_EQ(session.state(), TransferState::FAILED);
}

TEST(NearbyFileSession, SenderFinishesByHandingOverTheChecksum) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk, now);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(chunk, now);
  session.onEvent(event(TransferEventKind::ACK), now);

  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.kind, TransferActionKind::SEND_COMPLETE);
  EXPECT_EQ(session.state(), TransferState::VERIFYING);

  TransferEvent result = event(TransferEventKind::RESULT);
  result.success = true;
  session.onEvent(result, now);
  EXPECT_EQ(session.state(), TransferState::DONE);
}

TEST(NearbyFileSession, SenderReportsAFailedVerificationOnTheOtherSide) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk, now);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(chunk, now);
  session.onEvent(event(TransferEventKind::ACK), now);
  drain(session, now);

  TransferEvent result = event(TransferEventKind::RESULT);
  result.success = false;
  session.onEvent(result, now);
  EXPECT_EQ(session.state(), TransferState::FAILED);
}

TEST(NearbyFileSession, ReceiverAnswersAReaderLookingForSomewhereToSend) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginReceive(now);

  EXPECT_EQ(session.state(), TransferState::LISTENING);
  EXPECT_TRUE(drain(session, now).empty()) << "a listening reader must not transmit unprompted";

  session.onEvent(event(TransferEventKind::DISCOVER), now);
  const std::vector<TransferAction> actions = drain(session, now);
  ASSERT_TRUE(contains(actions, TransferActionKind::SEND_ADVERTISE));
  EXPECT_EQ(actions.front().peerMac, PEER_MAC);
}

TEST(NearbyFileSession, ReceiverWaitsForTheReaderBeforeAcceptingAnOffer) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginReceive(now);
  session.onEvent(event(TransferEventKind::DISCOVER), now);
  drain(session, now);

  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = 4096;
  offer.deviceName = "Lector-4B2C";
  session.onEvent(offer, now);

  EXPECT_EQ(session.state(), TransferState::OFFER_PROMPT);
  EXPECT_EQ(session.offeredName(), "Book.epub");
  EXPECT_EQ(session.offeredSize(), 4096u);
  // Nothing is accepted, and no file is opened, until the reader here says so.
  EXPECT_FALSE(contains(drain(session, now), TransferActionKind::SEND_ACCEPT));

  session.acceptOffer("/books/Book.epub", now);
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_ACCEPT));
  EXPECT_EQ(session.state(), TransferState::TRANSFERRING);
  EXPECT_EQ(session.destinationPath(), "/books/Book.epub");
}

TEST(NearbyFileSession, ReceiverCanTurnAnOfferDown) {
  uint32_t now = 1000;
  TransferSession session;
  session.beginReceive(now);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = 4096;
  session.onEvent(offer, now);

  session.rejectOffer(now);
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_REJECT));
  EXPECT_EQ(session.state(), TransferState::REJECTED);
}

TEST(NearbyFileSession, ReceiverAcknowledgesChunksInOrderAndRefusesGaps) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session;
  session.beginReceive(now);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = chunk * 2;
  session.onEvent(offer, now);
  session.acceptOffer("/books/Book.epub", now);
  drain(session, now);

  const std::vector<uint8_t> payload(chunk, 0x5A);
  EXPECT_TRUE(session.acceptChunk(0, payload.data(), payload.size(), now));
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_ACK));

  // A chunk out of order would tear a hole in the written file, so it is refused
  // and left unacknowledged, which makes the sender repeat the one that is due.
  EXPECT_FALSE(session.acceptChunk(5, payload.data(), payload.size(), now));
  // A repeat of the chunk already written is acknowledged again, not written twice.
  EXPECT_FALSE(session.acceptChunk(0, payload.data(), payload.size(), now));
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_ACK));

  EXPECT_TRUE(session.acceptChunk(1, payload.data(), payload.size(), now));
  EXPECT_EQ(session.transferredBytes(), chunk * 2);
}

TEST(NearbyFileSession, ReceiverConfirmsTheFileArrivedIntact) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session;
  session.beginReceive(now);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = chunk;
  session.onEvent(offer, now);
  session.acceptOffer("/books/Book.epub", now);
  drain(session, now);

  const std::vector<uint8_t> payload(chunk, 0x5A);
  ASSERT_TRUE(session.acceptChunk(0, payload.data(), payload.size(), now));
  drain(session, now);

  TransferEvent complete = event(TransferEventKind::COMPLETE);
  complete.crc32 = session.crc32();
  session.onEvent(complete, now);

  const std::vector<TransferAction> actions = drain(session, now);
  ASSERT_TRUE(contains(actions, TransferActionKind::SEND_RESULT));
  EXPECT_TRUE(actions.back().success);
  EXPECT_EQ(session.state(), TransferState::DONE);
}

TEST(NearbyFileSession, ReceiverRejectsAFileThatArrivedCorrupted) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session;
  session.beginReceive(now);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = chunk;
  session.onEvent(offer, now);
  session.acceptOffer("/books/Book.epub", now);
  drain(session, now);

  const std::vector<uint8_t> payload(chunk, 0x5A);
  ASSERT_TRUE(session.acceptChunk(0, payload.data(), payload.size(), now));
  drain(session, now);

  TransferEvent complete = event(TransferEventKind::COMPLETE);
  complete.crc32 = session.crc32() ^ 0xFFFFu;
  session.onEvent(complete, now);

  const std::vector<TransferAction> actions = drain(session, now);
  ASSERT_TRUE(contains(actions, TransferActionKind::SEND_RESULT));
  EXPECT_FALSE(actions.back().success);
  EXPECT_EQ(session.state(), TransferState::FAILED);
  // The activity keys its "delete the half-written file" step off this.
  EXPECT_TRUE(session.shouldDiscardPartialFile());
}

TEST(NearbyFileSession, IgnoresATransferPacketFromSomeoneElse) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk * 4, now);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(chunk, now);

  // A third reader in the room cannot advance someone else's transfer. Bytes
  // count as transferred when the real peer acknowledges them, so a stranger's
  // ACK must leave the total alone and the chunk still outstanding.
  session.onEvent(event(TransferEventKind::ACK, OTHER_MAC), now);
  EXPECT_FALSE(session.nextAction(now, action));
  EXPECT_EQ(session.transferredBytes(), 0u);

  now += CHUNK_RETRY_INTERVAL_MS;
  ASSERT_TRUE(session.nextAction(now, action));
  EXPECT_EQ(action.sequence, 0u);
  EXPECT_EQ(action.offset, 0u);
}

TEST(NearbyFileSession, EitherSideCanCancel) {
  uint32_t now = 1000;
  TransferSession session = acceptedSender(TransferSession::chunkBytes() * 4, now);

  session.cancel(now);
  EXPECT_TRUE(contains(drain(session, now), TransferActionKind::SEND_CANCEL));
  EXPECT_EQ(session.state(), TransferState::CANCELLED);

  uint32_t receiverNow = 1000;
  TransferSession receiver;
  receiver.beginReceive(receiverNow);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = 4096;
  receiver.onEvent(offer, receiverNow);
  receiver.acceptOffer("/books/Book.epub", receiverNow);
  drain(receiver, receiverNow);

  receiver.onEvent(event(TransferEventKind::CANCEL), receiverNow);
  EXPECT_EQ(receiver.state(), TransferState::CANCELLED);
  EXPECT_TRUE(receiver.shouldDiscardPartialFile());
}

TEST(NearbyFileSession, ReportsProgressWhileItRuns) {
  uint32_t now = 1000;
  const uint16_t chunk = TransferSession::chunkBytes();
  TransferSession session = acceptedSender(chunk * 4, now);

  EXPECT_EQ(session.progressPercent(), 0);

  TransferAction action;
  ASSERT_TRUE(session.nextAction(now, action));
  session.onChunkSent(chunk, now);
  session.onEvent(event(TransferEventKind::ACK), now);
  EXPECT_EQ(session.progressPercent(), 25);
}
