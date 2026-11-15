#include <HalStorage.h>
#include <SDCardManager.h>
#include <gtest/gtest.h>

// A dirty SdFat cache is lost by FsVolume::end(): the volume must be synced first, and a
// failed sync must leave the card mounted and the firmware's access intact.
TEST(UsbDriveStorageGuard, FailedFlushKeepsTheCardMountedAndUsable) {
  syncOk = false;
  EXPECT_FALSE(Storage.beginUsbDrive());
  EXPECT_EQ(syncCalls, 1);
  EXPECT_EQ(detachCalls, 0);
  HalFile file;
  EXPECT_TRUE(Storage.openFileForWrite("TEST", "/still.txt", file));
  syncOk = true;
}

TEST(UsbDriveStorageGuard, SleepFlushesBeforeUnmountAndStillSleepsOnFailure) {
  syncCalls = detachCalls = 0;
  Storage.prepareForDeepSleep();
  EXPECT_EQ(syncCalls, 1);
  EXPECT_EQ(detachCalls, 1);
  syncOk = false;
  Storage.prepareForDeepSleep();
  EXPECT_EQ(syncCalls, 2);
  EXPECT_EQ(detachCalls, 2);
  syncOk = true;
}

// Once USB Drive lends the card to a computer, no firmware file call may reach it,
// including through a handle that was opened before the handoff.
TEST(UsbDriveStorageGuard, NoCallReachesTheCardWhileTheHostHoldsIt) {
  HalFile before;
  ASSERT_TRUE(Storage.openFileForWrite("TEST", "/log.txt", before));
  const uint8_t byte = 1;
  EXPECT_EQ(before.write(&byte, 1), 1u);

  syncCalls = detachCalls = detachWithoutSync = 0;
  ASSERT_TRUE(Storage.beginUsbDrive());
  EXPECT_EQ(syncCalls, 1);
  EXPECT_EQ(detachCalls, 1);
  EXPECT_EQ(detachWithoutSync, 0);
  const int callsAtHandoff = cardCalls;

  EXPECT_FALSE(before.isOpen());
  EXPECT_EQ(before.write(&byte, 1), 0u);
  EXPECT_EQ(before.write(byte), 0u);
  before.flush();
  EXPECT_EQ(before.read(), -1);
  uint8_t buffer[4];
  EXPECT_EQ(before.read(buffer, sizeof(buffer)), 0);
  EXPECT_FALSE(before.seek(0));
  EXPECT_FALSE(before.seekCur(1));
  EXPECT_EQ(before.available(), 0);
  EXPECT_EQ(before.position(), 0u);
  char name[8];
  EXPECT_EQ(before.getName(name, sizeof(name)), 0u);
  uint16_t date = 0, time = 0;
  EXPECT_FALSE(before.getModifyDateTime(&date, &time));
  EXPECT_FALSE(before.rename("/moved.txt"));
  before.rewindDirectory();
  EXPECT_FALSE(before.openNextFile().isOpen());
  EXPECT_FALSE(before.close());

  HalFile after;
  EXPECT_FALSE(Storage.openFileForWrite("TEST", "/new.txt", after));
  EXPECT_FALSE(Storage.openFileForRead("TEST", "/new.txt", after));
  EXPECT_FALSE(Storage.open("/new.txt", O_RDWR).isOpen());
  EXPECT_FALSE(Storage.writeFile("/new.txt", "x"));
  EXPECT_FALSE(Storage.exists("/log.txt"));
  EXPECT_FALSE(Storage.ensureDirectoryExists("/dir"));
  EXPECT_EQ(Storage.readFile("/log.txt").length(), 0u);
  char text[8];
  EXPECT_EQ(Storage.readFileToBuffer("/log.txt", text, sizeof(text)), 0u);
  EXPECT_FALSE(Storage.rmdir("/dir"));
  EXPECT_EQ(Storage.sdTotalBytes(), 0u);
  Storage.prepareForDeepSleep();
  EXPECT_FALSE(Storage.mkdir("/dir"));
  EXPECT_FALSE(Storage.remove("/log.txt"));
  EXPECT_FALSE(Storage.rename("/log.txt", "/x.txt"));
  EXPECT_FALSE(Storage.removeDir("/dir"));
  EXPECT_EQ(Storage.sdUsedBytes(), 0u);

  {
    HalFile dropped = std::move(before);
  }
  EXPECT_EQ(cardCalls, callsAtHandoff);
}
