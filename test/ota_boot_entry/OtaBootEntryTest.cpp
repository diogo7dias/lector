#include <gtest/gtest.h>

#include "OtaBootEntry.h"

using namespace ota_boot;

TEST(OtaBootEntry, TheNewEntryDoesNotArmBootloaderRollback) {
  // A foreign firmware (stock, CrossInk) never calls
  // esp_ota_mark_app_valid_cancel_rollback(), so an entry written as
  // ESP_OTA_IMG_NEW becomes PENDING_VERIFY on its first boot and the
  // bootloader rolls the device straight back to Lector. UNDEFINED leaves
  // rollback disarmed, so whatever we flashed stays flashed.
  const SelectEntry entry = makeSelectEntry(7, 0x1234u);
  EXPECT_EQ(entry.ota_state, kOtaImgUndefined);
  EXPECT_EQ(entry.ota_seq, 7u);
  EXPECT_EQ(entry.crc, 0x1234u);
  for (unsigned char byte : entry.seq_label) EXPECT_EQ(byte, 0xFF);
}

TEST(OtaBootEntry, TheNextSeqSelectsTheRequestedOtaSlot) {
  // Bootloader picks the app partition with (seq - 1) % numOtaParts.
  EXPECT_EQ((nextSeqFor(4, 0, 2) - 1) % 2, 0u);
  EXPECT_EQ((nextSeqFor(4, 1, 2) - 1) % 2, 1u);
}

TEST(OtaBootEntry, TheNextSeqAlwaysOutranksTheActiveOne) {
  EXPECT_GT(nextSeqFor(4, 0, 2), 4u);
  EXPECT_GT(nextSeqFor(4, 1, 2), 4u);
  EXPECT_GT(nextSeqFor(0, 0, 2), 0u);
}

TEST(OtaBootEntry, ReRollbackPreventedByWritingUndefinedState) {
  // A failed attempt or foreign firmware must never leave an entry in
  // ESP_OTA_IMG_NEW or ESP_OTA_IMG_PENDING_VERIFY, which would cause the
  // bootloader to attempt rollback cycles. Writing kOtaImgUndefined ensures
  // the bootloader treats the image as definitive without arming rollback.
  const SelectEntry entry = makeSelectEntry(10, 0xABCDu);
  EXPECT_NE(entry.ota_state, kOtaImgNew);
  EXPECT_NE(entry.ota_state, kOtaImgPendingVerify);
  EXPECT_EQ(entry.ota_state, kOtaImgUndefined);
}
