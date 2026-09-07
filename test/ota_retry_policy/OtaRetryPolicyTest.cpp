#include <gtest/gtest.h>

#include "OtaRetryPolicy.h"

using namespace ota_retry;

TEST(OtaRetryPolicy, ADroppedDownloadIsWorthAnotherAttempt) {
  EXPECT_TRUE(shouldRetry(Failure::DOWNLOAD, 1));
  EXPECT_TRUE(shouldRetry(Failure::DOWNLOAD, 2));
}

TEST(OtaRetryPolicy, AttemptsAreCapped) {
  EXPECT_FALSE(shouldRetry(Failure::DOWNLOAD, MAX_ATTEMPTS));
  EXPECT_FALSE(shouldRetry(Failure::DOWNLOAD, MAX_ATTEMPTS + 1));
}

TEST(OtaRetryPolicy, AWrongDeviceImageIsNeverRetried) {
  // Every attempt would fetch the same image for the same wrong chip.
  EXPECT_FALSE(shouldRetry(Failure::WRONG_CHIP, 1));
}

TEST(OtaRetryPolicy, AFlashWriteFailureIsNeverRetried) {
  // The partition, not the link, refused the bytes; retrying rewrites the same
  // bytes to the same partition.
  EXPECT_FALSE(shouldRetry(Failure::FLASH_WRITE, 1));
}

TEST(OtaRetryPolicy, BackoffGrowsWithEachAttempt) {
  EXPECT_LT(backoffMs(1), backoffMs(2));
  EXPECT_LT(backoffMs(2), backoffMs(3));
}

TEST(OtaRetryPolicy, TheFirstBackoffIsShortEnoughToFeelLikeARetry) {
  EXPECT_LE(backoffMs(1), 2000u);
}

TEST(OtaRetryPolicy, ARetryResumesWhereTheTransferStopped) {
  EXPECT_EQ(resumeOffset(4096), 4096u);
}

TEST(OtaRetryPolicy, ARetryThatGotNothingStartsFromTheBeginning) {
  EXPECT_EQ(resumeOffset(0), 0u);
}

TEST(OtaRetryPolicy, AHeaderTooShortToJudgeIsNotAWrongChip) {
  // The chip id sits at offset 12; fewer bytes than that decide nothing.
  EXPECT_FALSE(isWrongChip(0x0005, 0xFFFF));
}

TEST(OtaRetryPolicy, AnUnknownDeviceChipAcceptsAnyImage) {
  EXPECT_FALSE(isWrongChip(0x0009, 0xFFFF));
}

TEST(OtaRetryPolicy, AMatchingChipIsAccepted) {
  EXPECT_FALSE(isWrongChip(0x0005, 0x0005));
}

TEST(OtaRetryPolicy, AMismatchedChipIsRefused) {
  EXPECT_TRUE(isWrongChip(0x0009, 0x0005));
}

// --- a transfer that ended clean but short ---------------------------------
//
// A connection can close in the middle of a body and still look like a
// completed request to the HTTP layer. Committing that hands the bootloader a
// truncated image; it has to be treated as a resumable drop instead.

TEST(OtaRetryPolicy, ATransferThatStoppedShortOfTheImageIsNotComplete) {
  EXPECT_TRUE(isShortTransfer(1000, 4096));
  EXPECT_TRUE(isShortTransfer(0, 4096));
}

TEST(OtaRetryPolicy, AFullTransferIsComplete) {
  EXPECT_FALSE(isShortTransfer(4096, 4096));
}

TEST(OtaRetryPolicy, MoreBytesThanExpectedIsNotCalledShort) {
  // Not this predicate's call to make: commit() validates the image itself.
  EXPECT_FALSE(isShortTransfer(5000, 4096));
}

TEST(OtaRetryPolicy, AServerThatNeverGaveASizeIsTakenAtItsWord) {
  // Chunked responses carry no Content-Length, so there is nothing to compare
  // and a completed transfer must not be failed on suspicion.
  EXPECT_FALSE(isShortTransfer(0, 0));
  EXPECT_FALSE(isShortTransfer(4096, 0));
}

TEST(OtaRetryPolicy, AShortTransferIsRetriedLikeAnyOtherDrop) {
  // It is reported as DOWNLOAD precisely so the existing resume path picks it
  // up: the partition keeps what arrived and the next attempt asks for the rest.
  EXPECT_TRUE(shouldRetry(Failure::DOWNLOAD, 1));
  EXPECT_EQ(resumeOffset(1000), 1000u);
}
