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
