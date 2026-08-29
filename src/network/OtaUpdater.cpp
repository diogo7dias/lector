#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <ReleaseJsonParser.h>
// clang-format on

#include <algorithm>
#include <cstring>
#include <string>

#include "FirmwareFlasher.h"
#include "FirmwareVersion.h"
#include "OtaRetryPolicy.h"

namespace {
// This fork's own releases, NOT upstream's. Pointed at crosspoint-reader until 0.24.1,
// which meant Check for Updates offered upstream CrossPoint builds: a different firmware,
// with different settings and a different feature set, installed straight over lector.
// Only stable releases are returned here — /releases/latest skips prereleases, and every
// release-candidate and experimental tag is published as one.
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/diogo7dias/lector/releases/latest";

// The full list, newest first, prereleases included. Only Install Other
// Firmware asks for this: on a reader whose USB flashing the vendor locked, a
// prerelease may be the only build that can rescue it, and /releases/latest
// hides prereleases by design.
constexpr char releaseListUrl[] = "https://api.github.com/repos/diogo7dias/lector/releases?per_page=1";

// A TLS session with WiFi up needs room the reader does not always have. Below
// this, wolfSSL fails mid-handshake and retries for a minute with a few hundred
// bytes free, which reads as a hang; refuse the attempt and say so instead.
// Same threshold the font download uses (FontDownloadActivity.h).
constexpr int MIN_HEAP_FOR_TLS = 30000;

bool isHttps(const std::string& url) { return url.rfind("https://", 0) == 0; }
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate(const bool includePrereleases) {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  if (ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
    LOG_ERR("OTA", "Only %u bytes free, need %d for a secure connection",
            static_cast<unsigned>(ESP.getFreeHeap()), MIN_HEAP_FOR_TLS);
    return OOM_ERROR;
  }

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser;
#ifdef FREEINK_DEVICE_X4PRO
  // The X4 Pro is an ESP32-S3; the release's plain firmware.bin is the C3 build
  // and the chip-id gate would refuse it. Ask for the S3 asset by name, and fall
  // back to firmware.bin when the release does not carry one -- which is also
  // what the OTA Unlocker always serves.
  releaseParser.setPreferredAssetName("firmware-x4pro.bin");
#endif
  releaseParser.setListMode(includePrereleases);
  const bool ok = HttpDownloader::fetchUrl(includePrereleases ? releaseListUrl : latestReleaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
    releaseParser.feed(reinterpret_cast<const char*>(data), len);
    return true;
  });
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_ERR("OTA", "No firmware.bin asset found");
    return NO_UPDATE;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  otaSize = releaseParser.getFirmwareSize();
  totalSize = otaSize;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }
  // Both sides carry a name around the numbers ("lector 0.24.1" here, "lector-0.24.2" as
  // the tag), so the comparison has to find the numbers rather than assume the string
  // starts with them — see FirmwareVersion.h.
  return firmware_version::isNewer(latestVersion.c_str(), CROSSPOINT_VERSION);
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

bool OtaUpdater::isDownloadSecure() const { return isHttps(otaUrl); }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx,
                                                      const bool allowAnyVersion) {
  // allowAnyVersion is the escape hatch for a device whose USB flashing the
  // vendor locked: the only way off this firmware is an update server, and the
  // firmware it offers is usually not "newer" than what is running -- it is
  // stock, or another fork, or this same version again. Refusing those leaves
  // the user stuck on lector with no way out, so the deliberate "install other
  // firmware" flow skips the comparison. Check for Updates still does not.
  if (!updateAvailable || otaUrl.empty()) return NO_UPDATE;
  if (!allowAnyVersion && !isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  if (isHttps(otaUrl) && ESP.getFreeHeap() < MIN_HEAP_FOR_TLS) {
    LOG_ERR("OTA", "Only %u bytes free, need %d for a secure connection", static_cast<unsigned>(ESP.getFreeHeap()),
            MIN_HEAP_FOR_TLS);
    return OOM_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  //
  // The bytes go through firmware_flash::StreamingInstall, the same raw
  // partition write the SD-card update uses, rather than esp_ota_*: esp_ota_end
  // runs esp_image_verify, which rejects images this device runs perfectly
  // (see FirmwareFlasher.h), and esp_ota_set_boot_partition arms a rollback
  // that sends any non-Arduino firmware straight back here on its first boot.
  firmware_flash::StreamingInstall installer;
  const firmware_flash::Result beginRes = installer.begin();
  if (beginRes != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "install begin failed: %s", firmware_flash::resultName(beginRes));
    return INTERNAL_UPDATE_ERROR;
  }

  processedSize = 0;
  int lastReportedPct = -1;
  ota_retry::Failure failure = ota_retry::Failure::DOWNLOAD;
  bool installed = false;

  // The image streams in chunks; only the first bytes carry the header. Buffer
  // the first 14 bytes so we can read chip_id (esp_image_header_t offset 12) and
  // reject a wrong-MCU image before it overwrites the OTA partition. Held across
  // attempts because a resumed transfer picks up exactly where the last one
  // stopped: a drop inside the first 14 bytes would otherwise leave the check
  // half-fed and never made.
  uint8_t hdr[14];
  size_t hdrLen = 0;

  // A 5 MB image over a marginal link drops often enough that one attempt is not
  // a fair test. The partition keeps what was already written, so an attempt
  // that stopped at 80% keeps its 80% and the next one asks the server for the
  // rest; only a link failure is worth another go (see OtaRetryPolicy.h).
  for (int attempt = 1; attempt <= ota_retry::MAX_ATTEMPTS; ++attempt) {
    bool wrongChip = false;
    bool flashOk = true;
    bool replayedFromStart = false;
    const size_t resumeFrom = ota_retry::resumeOffset(processedSize);

    const bool fetchOk = HttpDownloader::fetchUrl(
        otaUrl,
        [&](const uint8_t* data, size_t len) {
          if (hdrLen < sizeof(hdr)) {
            const size_t take = std::min(len, sizeof(hdr) - hdrLen);
            std::memcpy(hdr + hdrLen, data, take);
            hdrLen += take;
            if (hdrLen == sizeof(hdr)) {
              uint16_t imageChip;
              std::memcpy(&imageChip, hdr + 12, sizeof(imageChip));
              const uint16_t deviceChip = firmware_flash::runningPartitionChipId();
              if (ota_retry::isWrongChip(imageChip, deviceChip)) {
                LOG_ERR("OTA", "wrong chip: image=0x%04X device=0x%04X", imageChip, deviceChip);
                wrongChip = true;
                return false;  // abort the transfer
              }
            }
          }
          if (installer.feed(data, len) != firmware_flash::Result::OK) {
            flashOk = false;
            return false;  // abort the transfer
          }
          processedSize += len;
          // Fire the callback only on whole-percent change. Per-chunk updates wake the
          // render task, whose framebuffer work contends with TLS on the internal arena,
          // and e-ink can't repaint faster than a percent tick anyway.
          if (onProgress && totalSize > 0) {
            const int pct = static_cast<int>(static_cast<uint64_t>(processedSize) * 100 / totalSize);
            if (pct != lastReportedPct) {
              lastReportedPct = pct;
              onProgress(ctx);
            }
          }
          return true;
        },
        "", "", resumeFrom, &replayedFromStart);

    if (wrongChip) {
      failure = ota_retry::Failure::WRONG_CHIP;
    } else if (!flashOk) {
      failure = ota_retry::Failure::FLASH_WRITE;
    } else if (fetchOk) {
      installed = true;
      break;
    } else {
      failure = ota_retry::Failure::DOWNLOAD;
    }

    if (!ota_retry::shouldRetry(failure, attempt)) break;

    // A server that ignored the Range replayed the whole body, so the partition
    // holds the image twice over from here on. Start it again rather than write
    // a second copy onto the first.
    if (replayedFromStart) {
      installer.restart();
      processedSize = 0;
      lastReportedPct = -1;
      hdrLen = 0;
    }

    LOG_ERR("OTA", "Attempt %d stopped at %zu bytes; retrying", attempt, processedSize);
    delay(ota_retry::backoffMs(attempt));
  }

  if (!installed) {
    if (failure == ota_retry::Failure::WRONG_CHIP) {
      LOG_ERR("OTA", "Firmware install aborted: wrong device");
      return WRONG_DEVICE_ERROR;
    }
    LOG_ERR("OTA", "Firmware install failed (%s)",
            failure == ota_retry::Failure::FLASH_WRITE ? "flash write" : "download");
    return failure == ota_retry::Failure::FLASH_WRITE ? INTERNAL_UPDATE_ERROR : HTTP_ERROR;
  }

  // Validates the image where it landed, then points otadata at it with the
  // rollback left disarmed.
  const firmware_flash::Result commitRes = installer.commit();
  if (commitRes != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "commit failed: %s", firmware_flash::resultName(commitRes));
    if (commitRes == firmware_flash::Result::BAD_CHIP) return WRONG_DEVICE_ERROR;
    if (commitRes == firmware_flash::Result::OOM) return OOM_ERROR;
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
