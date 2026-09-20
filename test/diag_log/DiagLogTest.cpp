// The diagnostics buffer and the retention rule for /lector-flash-diagnostics.txt.
//
// The file exists so a user with no serial cable can paste one text file into a
// chat and have the fault be legible. These tests pin the three things that
// make it trustworthy: nothing is lost silently when the RAM buffer wraps, the
// file stays bounded on any clock, and nothing private ever reaches it.
#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "DiagLog.h"

namespace {

std::string ring() { return std::string(diaglog::data(), diaglog::size()); }

std::string retained(std::string file, uint32_t now, size_t cap = diaglog::kFileCapBytes) {
  std::string buf = file;
  const size_t kept = diaglog::retain(buf.data(), buf.size(), now, cap);
  return buf.substr(0, kept);
}

constexpr uint32_t kSep14 = 1789380000u;  // 2026-09-14T10:00:00Z
constexpr uint32_t kDay = 86400u;

class DiagLogTest : public ::testing::Test {
 protected:
  void SetUp() override { diaglog::clear(); }
};

// --- time ---------------------------------------------------------------------

TEST_F(DiagLogTest, UtcRoundTripsThroughTheHeaderLine) {
  char when[24];
  diaglog::formatUtc(kSep14, when, sizeof(when));
  EXPECT_STREQ(when, "2026-09-14T10:00:00Z");
  EXPECT_EQ(diaglog::utcFromCivil(2026, 9, 14, 10, 0, 0), kSep14);
  diaglog::beginEntry("flash attempt", "session=a3f9 attempt=1", {kSep14, 412});
  EXPECT_EQ(ring(), "=== flash attempt session=a3f9 attempt=1 utc=2026-09-14T10:00:00Z up=412s\n");
  EXPECT_EQ(diaglog::parseEntryUtc(diaglog::data(), diaglog::size()), kSep14);
}

TEST_F(DiagLogTest, AnUnsetClockIsWrittenAsUnsetNotAs1970) {
  diaglog::beginEntry("boot", nullptr, {0, 3});
  EXPECT_EQ(ring(), "=== boot utc=unset up=3s\n");
  EXPECT_EQ(diaglog::parseEntryUtc(diaglog::data(), diaglog::size()), 0u);
  // A raw 1970 epoch (the system clock before any sync) is also "unset".
  char when[24];
  diaglog::formatUtc(12345, when, sizeof(when));
  EXPECT_STREQ(when, "unset");
}

// --- the buffer ---------------------------------------------------------------

TEST_F(DiagLogTest, NoteAppendsWholeLinesAndNothingIsPendingUntilMarked) {
  diaglog::note("  battery=%u%%", 71u);
  diaglog::note("  heap free=%u", 140212u);
  EXPECT_EQ(ring(), "  battery=71%\n  heap free=140212\n");
  EXPECT_FALSE(diaglog::pending());
  diaglog::markPending();
  EXPECT_TRUE(diaglog::pending());
  diaglog::clear();
  EXPECT_EQ(diaglog::size(), 0u);
  EXPECT_FALSE(diaglog::pending());
}

TEST_F(DiagLogTest, WrapDropsOldestWholeLinesAndCountsThem) {
  // 40 lines of 60 bytes each is 2440 bytes, past the 2048-byte buffer.
  for (int i = 0; i < 40; ++i) diaglog::note("line %02d %s", i, std::string(50, 'x').c_str());
  EXPECT_LE(diaglog::size(), diaglog::kRingBytes);
  const std::string got = ring();
  // What survives is a run of complete lines ending with the newest one.
  EXPECT_EQ(got.substr(0, 5), "line ");
  EXPECT_EQ(got.substr(got.size() - 1), "\n");
  EXPECT_NE(got.find("line 39 "), std::string::npos);
  EXPECT_EQ(got.find("line 00 "), std::string::npos);
  EXPECT_GT(diaglog::droppedLines(), 0u);
  // Dropped count plus surviving lines equals what was written.
  size_t lines = 0;
  for (char c : got) lines += c == '\n';
  EXPECT_EQ(lines + diaglog::droppedLines(), 40u);
}

TEST_F(DiagLogTest, AnOverlongLineIsCutNotSplit) {
  diaglog::note("%s", std::string(500, 'y').c_str());
  EXPECT_EQ(diaglog::size(), diaglog::kLineBytes);  // kLineBytes - 1 chars + newline
  EXPECT_EQ(ring().back(), '\n');
}

// --- retention ------------------------------------------------------------------

std::string entry(const char* kind, uint32_t utc, const char* body = "  result=READ_FAIL\n") {
  char when[24];
  diaglog::formatUtc(utc, when, sizeof(when));
  return std::string("=== ") + kind + " utc=" + when + " up=10s\n" + body;
}

TEST_F(DiagLogTest, HeaderAndPartialLeadingEntryAreDropped) {
  const std::string file =
      "# lector diagnostics v2 firmware=x\n  orphan line from a tail read\n" + entry("flash attempt", kSep14);
  EXPECT_EQ(retained(file, kSep14), entry("flash attempt", kSep14));
}

TEST_F(DiagLogTest, EntriesOlderThanTwoDaysAreDroppedOnAValidClock) {
  const std::string old = entry("flash attempt session=1", kSep14 - 3 * kDay);
  const std::string recent = entry("flash attempt session=2", kSep14 - kDay);
  const std::string newest = entry("boot", kSep14);
  EXPECT_EQ(retained(old + recent + newest, kSep14), recent + newest);
}

TEST_F(DiagLogTest, ClockUnsetNowKeepsEverythingAgeCannotJudge) {
  const std::string old = entry("flash attempt", kSep14 - 30 * kDay);
  const std::string newest = entry("boot", kSep14);
  EXPECT_EQ(retained(old + newest, 0), old + newest);
}

TEST_F(DiagLogTest, EntriesWithNoClockAreKeptOnAnyClock) {
  const std::string unset = entry("flash attempt", 0);
  const std::string newest = entry("boot", kSep14);
  EXPECT_EQ(retained(unset + newest, kSep14 + 400 * kDay), unset + newest);
}

TEST_F(DiagLogTest, ClockJumpedForwardNeverEmptiesTheFile) {
  // Yesterday's failure, then NTP moves the clock a year ahead. The user is
  // about to send this file; the newest entry survives any clock.
  const std::string older = entry("flash attempt session=1", kSep14 - 2 * kDay);
  const std::string failure = entry("flash attempt session=2", kSep14 - kDay);
  EXPECT_EQ(retained(older + failure, kSep14 + 365 * kDay), failure);
}

TEST_F(DiagLogTest, ClockJumpedBackwardKeepsFutureEntries) {
  const std::string future = entry("flash attempt", kSep14 + 30 * kDay);
  const std::string later = entry("boot", kSep14 + 31 * kDay);
  EXPECT_EQ(retained(future + later, kSep14), future + later);
}

TEST_F(DiagLogTest, ByteCapDropsOldestEntriesFirst) {
  std::string file;
  for (int i = 0; i < 20; ++i) {
    file += entry(("flash attempt n=" + std::to_string(i)).c_str(), kSep14, "  filler filler filler filler\n");
  }
  const std::string kept = retained(file, kSep14, 300);
  EXPECT_LE(kept.size(), 300u);
  EXPECT_EQ(kept.find("n=0 "), std::string::npos);
  EXPECT_NE(kept.find("n=19 "), std::string::npos);
  EXPECT_EQ(kept.substr(0, 4), "=== ");
}

TEST_F(DiagLogTest, LegacyEntriesWithoutUtcAreKeptUntilTheCapPushesThemOut) {
  // The 0.31.x file: "=== install attempt ===" blocks with no timestamp.
  const std::string legacy = "=== install attempt ===\n  firmware: lector 0.31.4\n  image: /firmware.bin size=0\n";
  const std::string modern = entry("flash attempt", kSep14);
  EXPECT_EQ(retained(legacy + modern, kSep14), legacy + modern);
  EXPECT_EQ(retained(legacy + modern, kSep14, modern.size() + 5), modern);
}

// --- privacy -----------------------------------------------------------------------

TEST_F(DiagLogTest, ImageLabelKeepsRootNamesAndHidesFolders) {
  char out[48];
  diaglog::imageLabel("/firmware.bin", out, sizeof(out));
  EXPECT_STREQ(out, "/firmware.bin");
  diaglog::imageLabel("/Books/Diogo Dias/private-notes/firmware.bin", out, sizeof(out));
  EXPECT_STREQ(out, "(file in a folder)");
  EXPECT_EQ(std::string(out).find("Diogo"), std::string::npos);
  diaglog::imageLabel(nullptr, out, sizeof(out));
  EXPECT_STREQ(out, "unknown");
  const std::string longName = "/" + std::string(60, 'n') + ".bin";
  diaglog::imageLabel(longName.c_str(), out, sizeof(out));
  EXPECT_EQ(strlen(out), 41u);
}

// --- acceptance: two attempts, told apart from the file alone ---------------------

TEST_F(DiagLogTest, TwoAttemptsAreDistinguishableWithOffsetAndBattery) {
  diaglog::beginEntry("flash attempt", "session=a3f9 attempt=1", {kSep14, 412});
  diaglog::note("  source=sd image=/firmware.bin size=4325376");
  diaglog::note("  battery=%u%% %umV", 71u, 3910u);
  diaglog::note("  write: READ_FAIL at byte %u of %u", 2101248u, 4325376u);
  diaglog::note("  result=READ_FAIL stage=write");
  diaglog::beginEntry("flash attempt", "session=a3f9 attempt=2", {kSep14 + 90, 502});
  diaglog::note("  battery=%u%% %umV", 30u, 3620u);
  diaglog::note("  readback: VERIFY_FAIL at byte %u (0x%X)", 3145728u, 3145728u);
  diaglog::note("  result=VERIFY_FAIL stage=readback");
  const std::string file = ring();
  EXPECT_NE(file.find("attempt=1"), std::string::npos);
  EXPECT_NE(file.find("attempt=2"), std::string::npos);
  EXPECT_NE(file.find("result=READ_FAIL stage=write"), std::string::npos);
  EXPECT_NE(file.find("result=VERIFY_FAIL stage=readback"), std::string::npos);
  EXPECT_NE(file.find("0x300000"), std::string::npos);
  EXPECT_NE(file.find("3620mV"), std::string::npos);
  EXPECT_EQ(file.find("size=0 "), std::string::npos);
}

TEST_F(DiagLogTest, TenMinutesOfWifiCheckpointsRetainNewestWithinWholeFileCap) {
  std::string file;
  for (uint32_t ms = 0; ms <= 600000; ms += 5000) {
    diaglog::clear();
    diaglog::beginEntry("wifi connect", "session=1234", {0, ms / 1000});
    for (int line = 0; line < 12; ++line) diaglog::note("  heartbeat t=%u loop=%u line=%d", ms, ms / 10, line);
    const size_t room = diaglog::kFileCapBytes - diaglog::kHeaderReserveBytes - diaglog::size();
    file.resize(diaglog::retain(file.data(), file.size(), 0, room));
    file.append(diaglog::data(), diaglog::size());
    EXPECT_LE(file.size() + diaglog::kHeaderReserveBytes, diaglog::kFileCapBytes);
    EXPECT_EQ(diaglog::droppedLines(), 0u);
  }
  EXPECT_NE(file.find("t=600000"), std::string::npos);
  EXPECT_EQ(file.find("t=0 "), std::string::npos);
}

TEST_F(DiagLogTest, WifiWriteBudgetBoundsEvenAHotRetryLoopAcrossClockWrap) {
  for (const uint32_t start : {0u, UINT32_MAX - 2000u}) {
    uint32_t writes = 0;
    uint32_t last = start;
    for (uint32_t elapsed = 0; elapsed <= 600000; elapsed += 10) {
      const uint32_t now = start + elapsed;
      if (diaglog::wifiCheckpointDue(writes, now, last, true)) {
        ++writes;
        last = now;
      }
    }
    EXPECT_LE(writes, 48u + 600000u / 5000u);
    EXPECT_GE(writes, 48u + 600000u / 5000u - 1);
    EXPECT_FALSE(diaglog::wifiCheckpointDue(writes, last + 4999, last, true));
    EXPECT_TRUE(diaglog::wifiCheckpointDue(writes, last + 5000, last, false));
  }
}

}  // namespace
