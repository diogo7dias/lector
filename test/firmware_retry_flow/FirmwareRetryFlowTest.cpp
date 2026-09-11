// Source audit: a firmware update that fails leaves the reader on a screen it
// can retry from, and a retry can never be the thing that bricks the device.
//
// These are contracts about control flow, not about pixels, and the activities
// they cover cannot be built on the host (Arduino, esp_ota, WiFi). So they are
// checked by reading the source, the same way test/status_screens and
// test/device_look do.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

std::string readSource(const char* path) {
  std::ifstream file(path);
  EXPECT_TRUE(file.is_open()) << "cannot open " << path;
  std::stringstream text;
  text << file.rdbuf();
  return text.str();
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

}  // namespace

// --- the failure screen offers a way on, not just a way out -----------------

TEST(FirmwareRetryFlow, TheOtaFailureScreenOffersRetryAndBack) {
  const std::string source = readSource(OTA_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(source, "view.acceptLabel = tr(STR_RETRY)"))
      << "the OTA failure screen has no Retry; a failed update means walking the settings tree again";
  EXPECT_TRUE(contains(source, "view.confirmHint = tr(STR_RETRY)")) << "Retry is drawn but no key is bound to it";
  EXPECT_TRUE(contains(source, "view.cancelLabel = tr(STR_BACK)")) << "the failure screen has no way out";
}

TEST(FirmwareRetryFlow, TheOtaConfirmButtonRetriesFromTheFailureScreen) {
  const std::string source = readSource(OTA_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(source, "if (state == FAILED) {\n    retryFailedStep();"))
      << "Confirm on the failure screen does not retry";
}

TEST(FirmwareRetryFlow, TheSdFailureScreenOffersRetry) {
  const std::string source = readSource(SD_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(source, "view.acceptLabel = tr(STR_RETRY)")) << "the SD update failure screen has no Retry";
  EXPECT_TRUE(contains(source, "canRetryFlash")) << "the SD failure screen offers Retry with nothing to retry";
}

// --- retrying is unbounded --------------------------------------------------

TEST(FirmwareRetryFlow, NothingCapsHowManyTimesTheReaderMayRetry) {
  // The bounded retry belongs inside a single install attempt (OtaRetryPolicy).
  // A cap on the manual button would strand a reader who has just moved closer
  // to the router. `manualRetries > 0` is allowed: that is the attempt counter
  // deciding whether to print itself, not a limit.
  for (const char* path : {OTA_ACTIVITY_SOURCE, SD_ACTIVITY_SOURCE}) {
    const std::string source = readSource(path);
    EXPECT_TRUE(contains(source, "manualRetries")) << path << " does not track manual retries at all";
    EXPECT_FALSE(contains(source, "MAX_MANUAL_RETRIES"))
        << path << " caps manual retries; Retry must work indefinitely";
    // Every comparison of the counter must be the "have we retried at all" test.
    for (std::size_t at = source.find("manualRetries >"); at != std::string::npos;
         at = source.find("manualRetries >", at + 1)) {
      EXPECT_EQ(source.compare(at, std::string("manualRetries > 0").size(), "manualRetries > 0"), 0)
          << path << " compares manualRetries against a limit; Retry must work indefinitely";
    }
    for (std::size_t at = source.find("manualRetries <"); at != std::string::npos;
         at = source.find("manualRetries <", at + 1)) {
      ADD_FAILURE() << path << " gates a retry on the attempt count; Retry must work indefinitely";
    }
  }
}

// --- a retry never destroys the image the device can still boot -------------

TEST(FirmwareRetryFlow, TheSdRetryRevalidatesBeforeItFlashes) {
  // The card is removable, so a retry must not trust the previous pass's
  // verdict about the bytes on it.
  const std::string source = readSource(SD_ACTIVITY_SOURCE);
  const std::size_t retry = source.find("void SdFirmwareUpdateActivity::onConfirmButton()");
  ASSERT_NE(retry, std::string::npos);
  const std::string body = source.substr(retry);
  EXPECT_TRUE(contains(body, "validateFirmware()")) << "the SD retry re-flashes without re-validating the image";
}

TEST(FirmwareRetryFlow, OtadataIsOnlySwitchedAfterTheFlashedImageValidates) {
  // The one invariant that keeps a failed update from bricking the reader: the
  // boot pointer moves only after the bytes in flash have been checked where
  // they landed.
  const std::string source = readSource(FLASHER_SOURCE);
  const std::size_t commit = source.find("Result StreamingInstall::commit()");
  ASSERT_NE(commit, std::string::npos);
  const std::string body = source.substr(commit);
  const std::size_t validate = body.find("validateFlashedImage");
  const std::size_t switchTo = body.find("ota_boot::switchTo");
  ASSERT_NE(validate, std::string::npos) << "commit() no longer validates the flashed image";
  ASSERT_NE(switchTo, std::string::npos);
  EXPECT_LT(validate, switchTo) << "otadata is switched before the image is validated";
}

// --- a replayed body is discarded before it is written, not after -----------

TEST(FirmwareRetryFlow, AnIgnoredRangeRewindsBeforeTheFirstChunkIsWritten) {
  // A server that answers a ranged request with the whole body starts again at
  // byte 0. Folding that in after the attempt appends a second whole copy onto
  // the partial already in the partition.
  const std::string source = readSource(OTA_UPDATER_SOURCE);
  const std::size_t rewind = source.find("installer.restart()");
  const std::size_t write = source.find("installer.feed(data, len)");
  ASSERT_NE(rewind, std::string::npos) << "a replayed body is never discarded";
  ASSERT_NE(write, std::string::npos);
  EXPECT_LT(rewind, write) << "the replayed body is written before the partial is discarded";
}

TEST(FirmwareRetryFlow, AShortTransferIsNotTreatedAsAFinishedInstall) {
  const std::string source = readSource(OTA_UPDATER_SOURCE);
  EXPECT_TRUE(contains(source, "ota_retry::isShortTransfer(processedSize, totalSize)"))
      << "a body that ended early still counts as a completed download";
}

// --- diagnostics survive a failure ------------------------------------------

TEST(FirmwareRetryFlow, EveryFailurePathStillLogs) {
  for (const char* path : {OTA_ACTIVITY_SOURCE, SD_ACTIVITY_SOURCE, OTA_UPDATER_SOURCE}) {
    const std::string source = readSource(path);
    EXPECT_TRUE(contains(source, "LOG_ERR") || contains(source, "LOG_DBG"))
        << path << " reports no diagnostics on failure";
  }
}

TEST(FirmwareRetryFlow, TheDiagnosticsFileIsAppendedToNotReplaced) {
  // Storage.openFileForWrite opens O_TRUNC, so every record wiped the one
  // before it and the file only ever held its last line; the boot record then
  // erased the failure it was meant to explain.
  const std::string source = readSource(DIAGNOSTICS_SOURCE);
  EXPECT_FALSE(contains(source, "openFileForWrite"))
      << "diagnostics are opened with O_TRUNC; each record wipes the last";
  EXPECT_TRUE(contains(source, "O_APPEND")) << "diagnostics are not appended";
}

// --- a plain update check never offers another fork's firmware -------------

TEST(FirmwareRetryFlow, CheckForUpdatesNeverFallsBackToUpstream) {
  // The upstream CrossPoint endpoint exists for Install Other Firmware on a
  // reader behind an OTA unlocker. Reachable from the plain check, one dropped
  // TLS handshake against this fork's endpoint offered upstream's v1.x as a
  // "newer" lector, and the update screen then installed a different firmware.
  const std::string source = readSource(OTA_UPDATER_SOURCE);
  EXPECT_TRUE(contains(source, "if (includePrereleases) candidateUrls[numCandidates++] = upstreamReleaseUrl;"))
      << "upstream is not gated on Install Other Firmware";
  std::size_t uses = 0;
  for (std::size_t at = source.find("= upstreamReleaseUrl;"); at != std::string::npos;
       at = source.find("= upstreamReleaseUrl;", at + 1)) {
    ++uses;
  }
  EXPECT_EQ(uses, 1u) << "upstream is queued on a second path";
}

TEST(FirmwareRetryFlow, TheFailureScreenStillNamesWhatWentWrong) {
  // Retry must not have replaced the detail lines: on a USB-locked reader a
  // photo of this screen is often the only report anyone gets.
  const std::string source = readSource(OTA_ACTIVITY_SOURCE);
  EXPECT_TRUE(contains(source, "failedDetail"));
  EXPECT_TRUE(contains(source, "failedExtra"));
  EXPECT_TRUE(contains(source, "detailFor(error)"));
}
