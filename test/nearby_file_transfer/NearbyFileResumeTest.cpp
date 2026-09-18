#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "lib/NearbyFileTransfer/NearbyFilePayloads.h"
#include "lib/NearbyFileTransfer/NearbyFileSession.h"

// An interrupted transfer that picks up where it stopped has exactly one way to
// go wrong quietly: the checksum. Both ends hash the whole file in file order,
// so a resume that starts hashing at the offset reaches the end with a value
// over the tail alone. That does not corrupt the bytes on the card, it fails
// verification, which deletes a file that had actually arrived intact — and a
// resume that skipped the check entirely would keep a file nobody verified.
//
// These tests run the same file through a straight transfer and through an
// interrupted one, and require the two to agree byte for byte and checksum for
// checksum.

namespace {

using namespace nearby_file;

constexpr std::array<uint8_t, 6> PEER_MAC = {0xaa, 0xbb, 0xcc, 0x01, 0x02, 0x03};

TransferEvent event(const TransferEventKind kind) {
  TransferEvent incoming;
  incoming.kind = kind;
  incoming.sourceMac = PEER_MAC;
  return incoming;
}

std::vector<uint8_t> fileOfSize(const size_t size) {
  // Varied content on purpose: a file of one repeated byte hashes the same
  // however its chunks are ordered, so it would not catch a prefix that was
  // hashed in the wrong place or not at all.
  std::vector<uint8_t> file(size);
  for (size_t index = 0; index < size; index++) file[index] = static_cast<uint8_t>(index * 31 + 7);
  return file;
}

/** Reads back an already-received prefix, as the activity reads the ".part" file. */
TransferSession::PrefixReader readerOver(const std::vector<uint8_t>& bytes) {
  return [&bytes](const uint64_t offset, const uint16_t length) -> const uint8_t* {
    if (offset + length > bytes.size()) return nullptr;
    return bytes.data() + offset;
  };
}

struct TransferRun {
  /** What the receiver wrote to the card, in the order it wrote it. */
  std::vector<uint8_t> written;
  /** The lowest offset the sender put on the air; the file size when it sent nothing. */
  uint64_t lowestOffsetSent = UINT64_MAX;
  int chunksSent = 0;
};

/**
 * Drives both sides until they finish or `stopAfterBytes` have been written,
 * standing in for the activity: it moves chunks across, writes what the receiver
 * accepts, and carries acknowledgements back.
 */
TransferRun pump(TransferSession& sender, TransferSession& receiver, const std::vector<uint8_t>& file,
                 std::vector<uint8_t> alreadyWritten, const uint64_t stopAfterBytes, uint32_t& nowMs) {
  TransferRun run;
  run.written = std::move(alreadyWritten);

  TransferAction action;
  // Bounded so a stall fails the test rather than hanging it.
  for (int step = 0; step < 4000; step++) {
    if (sender.state() == TransferState::DONE || sender.state() == TransferState::FAILED) break;
    if (run.written.size() >= stopAfterBytes) break;
    if (!sender.nextAction(nowMs, action)) {
      nowMs += CHUNK_RETRY_INTERVAL_MS;
      continue;
    }

    if (action.kind == TransferActionKind::SEND_DATA) {
      run.lowestOffsetSent = std::min(run.lowestOffsetSent, action.offset);
      run.chunksSent++;
      const uint8_t* slice = file.data() + action.offset;
      sender.onChunkSent(slice, action.length, nowMs);
      if (receiver.acceptChunk(action.sequence, slice, action.length, nowMs)) {
        // The card only ever grows: a resumed receiver seeks to the end of what
        // it already has and appends from there.
        run.written.insert(run.written.end(), slice, slice + action.length);
      }
    } else if (action.kind == TransferActionKind::SEND_COMPLETE) {
      TransferEvent complete = event(TransferEventKind::COMPLETE);
      complete.crc32 = sender.crc32();
      receiver.onEvent(complete, nowMs);
    }

    TransferAction reply;
    while (receiver.nextAction(nowMs, reply)) {
      if (reply.kind == TransferActionKind::SEND_ACK) {
        TransferEvent acknowledgement = event(TransferEventKind::ACK);
        acknowledgement.sequence = reply.sequence;
        sender.onEvent(acknowledgement, nowMs);
      } else if (reply.kind == TransferActionKind::SEND_RESULT) {
        TransferEvent result = event(TransferEventKind::RESULT);
        result.success = reply.success;
        sender.onEvent(result, nowMs);
      }
    }
  }
  return run;
}

/** A sender that has found its reader and is waiting on the accept. */
TransferSession offeringSender(const uint64_t fileSize, const uint32_t nowMs) {
  TransferSession sender;
  sender.beginSend("Book.epub", fileSize, nowMs);
  sender.onEvent(event(TransferEventKind::ADVERTISE), nowMs);
  sender.choosePeer(PEER_MAC, nowMs);
  TransferAction drained;
  while (sender.nextAction(nowMs, drained)) {
  }
  return sender;
}

/** A receiver that has been offered the file and is waiting on the reader here. */
TransferSession promptedReceiver(const uint64_t fileSize, const uint32_t nowMs) {
  TransferSession receiver;
  receiver.beginReceive(nowMs);
  TransferEvent offer = event(TransferEventKind::OFFER);
  offer.fileName = "Book.epub";
  offer.fileSize = fileSize;
  receiver.onEvent(offer, nowMs);
  return receiver;
}

/** Carries the accept across, resume offset and all, exactly as the radio does. */
void deliverAccept(TransferSession& sender, TransferSession& receiver, const std::vector<uint8_t>& sourceFile,
                   const uint32_t nowMs) {
  TransferAction action;
  uint64_t resumeBytes = 0;
  bool sawAccept = false;
  while (receiver.nextAction(nowMs, action)) {
    if (action.kind != TransferActionKind::SEND_ACCEPT) continue;
    sawAccept = true;
    // Through the wire format rather than around it, so a resume offset that
    // does not survive encoding fails here rather than on two devices.
    std::array<uint8_t, 32> packet = {};
    size_t packetLength = 0;
    ASSERT_TRUE(encodeAcceptPayload(action.offset, packet.data(), packet.size(), packetLength));
    ASSERT_TRUE(decodeAcceptPayload(packet.data(), packetLength, resumeBytes));
  }
  ASSERT_TRUE(sawAccept);

  TransferEvent accept = event(TransferEventKind::ACCEPT);
  accept.resumeBytes = resumeBytes;
  sender.onEvent(accept, nowMs);
  ASSERT_EQ(sender.pendingResumeBytes(), resumeBytes);
  if (resumeBytes > 0) ASSERT_TRUE(sender.resumeFrom(resumeBytes, readerOver(sourceFile)));
}

constexpr uint16_t CHUNK = TransferSession::chunkBytes();

}  // namespace

TEST(NearbyFileResume, PicksUpWhereItStoppedAndAgreesOnTheWholeFileChecksum) {
  // A last chunk that is not full on purpose: the tail is where an off-by-one in
  // the resumed offset would show up.
  const std::vector<uint8_t> file = fileOfSize(CHUNK * 5 + 37);

  uint32_t now = 1000;
  TransferSession straightSender = offeringSender(file.size(), now);
  TransferSession straightReceiver = promptedReceiver(file.size(), now);
  straightReceiver.acceptOffer("/Book.epub", now);
  deliverAccept(straightSender, straightReceiver, file, now);
  const TransferRun straight = pump(straightSender, straightReceiver, file, {}, UINT64_MAX, now);

  ASSERT_EQ(straight.written, file);
  ASSERT_EQ(straightReceiver.state(), TransferState::DONE);
  const uint32_t wholeFileCrc = straightSender.crc32();

  // Now the same file, cut off after two chunks.
  now = 1000;
  TransferSession firstSender = offeringSender(file.size(), now);
  TransferSession firstReceiver = promptedReceiver(file.size(), now);
  firstReceiver.acceptOffer("/Book.epub", now);
  deliverAccept(firstSender, firstReceiver, file, now);
  const TransferRun interrupted = pump(firstSender, firstReceiver, file, {}, CHUNK * 2, now);

  ASSERT_EQ(interrupted.written.size(), CHUNK * 2u);
  firstReceiver.cancel(now);
  EXPECT_TRUE(firstReceiver.hasResumablePartialFile()) << "a transfer that merely stopped keeps its partial file";
  EXPECT_FALSE(firstReceiver.shouldDiscardPartialFile());

  // Both readers come back, and the receiver says how much it already holds.
  now = 9000;
  TransferSession sender = offeringSender(file.size(), now);
  TransferSession receiver = promptedReceiver(file.size(), now);
  ASSERT_TRUE(receiver.resumeFrom(interrupted.written.size(), readerOver(interrupted.written)));
  EXPECT_EQ(receiver.resumeBytes(), CHUNK * 2u);
  receiver.acceptOffer("/Book.epub", now);
  deliverAccept(sender, receiver, file, now);
  EXPECT_EQ(sender.resumeBytes(), CHUNK * 2u);

  const TransferRun resumed = pump(sender, receiver, file, interrupted.written, UINT64_MAX, now);

  EXPECT_EQ(resumed.written, file) << "the resumed file must be the same bytes as one sent straight through";
  EXPECT_EQ(receiver.crc32(), wholeFileCrc) << "the receiver must hash the prefix it already had, not just the tail";
  EXPECT_EQ(sender.crc32(), wholeFileCrc);
  EXPECT_EQ(receiver.state(), TransferState::DONE);
  EXPECT_EQ(sender.state(), TransferState::DONE);
  EXPECT_EQ(resumed.lowestOffsetSent, CHUNK * 2u) << "nothing before the resume point may go over the air again";
  EXPECT_EQ(resumed.chunksSent, 4) << "chunks 2, 3, 4 and the short tail, and no others";
}

TEST(NearbyFileResume, RefusesAnOffsetNeitherSideCouldNumber) {
  const std::vector<uint8_t> file = fileOfSize(CHUNK * 4);
  const uint32_t now = 1000;

  TransferSession receiver = promptedReceiver(file.size(), now);
  // Part way through a chunk: there is no sequence number for it, and resuming
  // there would leave the two ends counting different chunks.
  EXPECT_FALSE(receiver.resumeFrom(CHUNK + 17, readerOver(file)));
  // The whole file: nothing left to send, and nothing to verify against.
  EXPECT_FALSE(receiver.resumeFrom(file.size(), readerOver(file)));
  EXPECT_FALSE(receiver.resumeFrom(file.size() + CHUNK, readerOver(file)));
  EXPECT_FALSE(receiver.resumeFrom(0, readerOver(file)));
  EXPECT_EQ(receiver.resumeBytes(), 0u);
  EXPECT_EQ(receiver.crc32(), TransferSession{}.crc32()) << "a refused resume must not have hashed anything";
}

TEST(NearbyFileResume, LeavesTheSessionAtZeroWhenThePartialCannotBeReadBack) {
  const std::vector<uint8_t> file = fileOfSize(CHUNK * 4);
  const uint32_t now = 1000;

  TransferSession receiver = promptedReceiver(file.size(), now);
  int reads = 0;
  EXPECT_FALSE(receiver.resumeFrom(CHUNK * 3, [&](const uint64_t offset, const uint16_t) -> const uint8_t* {
    // The card gives up half way, which is the case that would otherwise leave a
    // checksum over part of a prefix and fail verification at the very end.
    if (++reads > 1) return nullptr;
    return file.data() + offset;
  }));
  EXPECT_EQ(receiver.resumeBytes(), 0u);
  EXPECT_EQ(receiver.transferredBytes(), 0u);
  EXPECT_EQ(receiver.crc32(), TransferSession{}.crc32());

  // And the transfer still works from the start afterwards.
  uint32_t nowMs = now;
  TransferSession sender = offeringSender(file.size(), nowMs);
  receiver.acceptOffer("/Book.epub", nowMs);
  deliverAccept(sender, receiver, file, nowMs);
  const TransferRun run = pump(sender, receiver, file, {}, UINT64_MAX, nowMs);
  EXPECT_EQ(run.written, file);
  EXPECT_EQ(receiver.state(), TransferState::DONE);
}

TEST(NearbyFileResume, ThrowsAwayAPartialWhoseChecksumWasRejected) {
  const std::vector<uint8_t> file = fileOfSize(CHUNK * 2);
  uint32_t now = 1000;

  TransferSession sender = offeringSender(file.size(), now);
  TransferSession receiver = promptedReceiver(file.size(), now);
  receiver.acceptOffer("/Book.epub", now);
  deliverAccept(sender, receiver, file, now);
  pump(sender, receiver, file, {}, UINT64_MAX, now);
  ASSERT_EQ(receiver.state(), TransferState::DONE);

  // A second run where the checksum does not match: the bytes on the card are
  // wrong, so a later resume must not build on them.
  now = 1000;
  TransferSession badSender = offeringSender(file.size(), now);
  TransferSession badReceiver = promptedReceiver(file.size(), now);
  badReceiver.acceptOffer("/Book.epub", now);
  deliverAccept(badSender, badReceiver, file, now);
  pump(badSender, badReceiver, file, {}, CHUNK, now);
  TransferEvent complete = event(TransferEventKind::COMPLETE);
  complete.crc32 = 0xDEADBEEF;
  badReceiver.onEvent(complete, now);

  EXPECT_EQ(badReceiver.state(), TransferState::FAILED);
  EXPECT_TRUE(badReceiver.shouldDiscardPartialFile());
  EXPECT_FALSE(badReceiver.hasResumablePartialFile());
}

TEST(NearbyFileResume, AnAcceptWithNoPayloadStillMeansFromTheStart) {
  // Firmware that predates resume sends an empty accept. It has to decode to a
  // transfer from zero rather than fail, or the two versions cannot talk.
  uint64_t resumeBytes = 12345;
  EXPECT_TRUE(decodeAcceptPayload(nullptr, 0, resumeBytes));
  EXPECT_EQ(resumeBytes, 0u);

  const uint8_t empty[1] = {0};
  resumeBytes = 12345;
  EXPECT_TRUE(decodeAcceptPayload(empty, 0, resumeBytes));
  EXPECT_EQ(resumeBytes, 0u);

  // A payload that is present but cut short is refused rather than half-read.
  resumeBytes = 12345;
  EXPECT_FALSE(decodeAcceptPayload(empty, 1, resumeBytes));
}

TEST(NearbyFileResume, CarriesEveryOffsetThroughTheWireFormat) {
  for (const uint64_t value : {0ULL, 1024ULL, 4096ULL, 1ULL << 32, 0x0123456789ABCDEFULL}) {
    std::array<uint8_t, 32> packet = {};
    size_t length = 0;
    ASSERT_TRUE(encodeAcceptPayload(value, packet.data(), packet.size(), length));
    uint64_t decoded = 0;
    ASSERT_TRUE(decodeAcceptPayload(packet.data(), length, decoded));
    EXPECT_EQ(decoded, value);
  }
}
