#include <gtest/gtest.h>

#include <array>
#include <string>

#include "lib/NearbyFileTransfer/NearbyFileGroup.h"

namespace {

using namespace nearby_file;

constexpr std::array<uint8_t, 6> SENDER = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
constexpr std::array<uint8_t, 6> STRANGER = {0x02, 0x99, 0x88, 0x77, 0x66, 0x55};

OfferPayload faceOffer(const uint8_t index, const uint8_t count = 3, const std::string& folder = ".fonts/Literata") {
  OfferPayload offer;
  offer.deviceName = "Lector";
  offer.fileName = "Literata_" + std::to_string(12 + index * 2) + ".cpfont";
  offer.fileSize = 40000;
  offer.folder = folder;
  offer.groupIndex = index;
  offer.groupCount = count;
  offer.groupTotalBytes = 120000;
  return offer;
}

}  // namespace

TEST(NearbyFileGroup, AsksTheReaderAboutALooseFile) {
  ReceiveGroup group;
  OfferPayload book;
  book.fileName = "Book.epub";
  book.fileSize = 1000;

  EXPECT_EQ(group.decide(book, SENDER, 0), GroupDecision::PROMPT);
  EXPECT_FALSE(group.expectsMore());
}

TEST(NearbyFileGroup, AsksOnceThenTakesTheRestOfTheFamily) {
  ReceiveGroup group;
  const uint32_t start = 1000;

  ASSERT_EQ(group.decide(faceOffer(0), SENDER, start), GroupDecision::PROMPT);
  group.onAccepted(faceOffer(0), SENDER, start);
  EXPECT_TRUE(group.expectsMore());
  EXPECT_EQ(group.folder(), ".fonts/Literata");

  group.onFileDone(start + 100);
  EXPECT_EQ(group.decide(faceOffer(1), SENDER, start + 200), GroupDecision::AUTO_ACCEPT);
  group.onAccepted(faceOffer(1), SENDER, start + 200);
  group.onFileDone(start + 300);

  EXPECT_EQ(group.decide(faceOffer(2), SENDER, start + 400), GroupDecision::AUTO_ACCEPT);
  group.onAccepted(faceOffer(2), SENDER, start + 400);
  group.onFileDone(start + 500);

  EXPECT_FALSE(group.expectsMore());
  EXPECT_EQ(group.filesDone(), 3);
}

TEST(NearbyFileGroup, RefusesAContinuationNobodyAcceptedFirst) {
  // Arriving in the middle of a family means the reader here never saw the
  // prompt, so nothing may be written on the strength of it.
  ReceiveGroup group;
  EXPECT_EQ(group.decide(faceOffer(1), SENDER, 0), GroupDecision::REJECT);
}

TEST(NearbyFileGroup, RefusesASecondSenderJoiningAFamilyInProgress) {
  ReceiveGroup group;
  ASSERT_EQ(group.decide(faceOffer(0), SENDER, 0), GroupDecision::PROMPT);
  group.onAccepted(faceOffer(0), SENDER, 0);
  group.onFileDone(10);

  EXPECT_EQ(group.decide(faceOffer(1), STRANGER, 20), GroupDecision::REJECT);
  EXPECT_EQ(group.decide(faceOffer(1, 3, ".fonts/Other"), SENDER, 20), GroupDecision::REJECT);
  EXPECT_EQ(group.decide(faceOffer(2), SENDER, 20), GroupDecision::REJECT);
  EXPECT_EQ(group.decide(faceOffer(1, 9), SENDER, 20), GroupDecision::REJECT);
}

TEST(NearbyFileGroup, GivesUpOnAFamilyThatStopsPartWayThrough) {
  ReceiveGroup group;
  group.onAccepted(faceOffer(0), SENDER, 0);
  group.onFileDone(100);

  EXPECT_FALSE(group.isStale(100 + GROUP_NEXT_FILE_TIMEOUT_MS - 1));
  EXPECT_TRUE(group.isStale(100 + GROUP_NEXT_FILE_TIMEOUT_MS));

  group.reset();
  EXPECT_FALSE(group.expectsMore());
  EXPECT_FALSE(group.isStale(1000000));
}

TEST(NearbyFileGroup, CountsTheFamilyForTheReaderPrompt) {
  ReceiveGroup group;
  const OfferPayload offer = faceOffer(0);
  group.onAccepted(offer, SENDER, 0);
  EXPECT_EQ(group.fileCount(), 3);
  EXPECT_EQ(group.totalBytes(), 120000u);
  EXPECT_EQ(group.familyName(), "Literata");
}
