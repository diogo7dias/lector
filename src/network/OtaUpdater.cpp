#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
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
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser;
  const bool ok = HttpDownloader::fetchUrl(latestReleaseUrl, [&releaseParser](const uint8_t* data, size_t len) {
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

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  // esp_https_ota is hardwired to esp-tls/mbedTLS, whose precompiled build on this
  // package can't negotiate TLS 1.3 (see SecureClient.h). Drive the OTA partition
  // ourselves and stream the firmware through HttpDownloader, which runs over
  // wolfSSL when FREEINK_NET_WOLFSSL is set, reusing its redirect handling for the
  // GitHub -> CDN hop.
  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }

  esp_ota_handle_t otaHandle = 0;
  esp_err_t esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_begin failed: %s", esp_err_to_name(esp_err));
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
  // a fair test. esp_ota_write appends, so an attempt that stopped at 80% keeps
  // its 80% and the next one asks the server for the rest; only a link failure
  // is worth another go (see OtaRetryPolicy.h).
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
          if (esp_ota_write(otaHandle, data, len) != ESP_OK) {
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
      esp_ota_abort(otaHandle);
      otaHandle = 0;
      esp_err = esp_ota_begin(updatePartition, OTA_SIZE_UNKNOWN, &otaHandle);
      if (esp_err != ESP_OK) {
        LOG_ERR("OTA", "esp_ota_begin failed on restart: %s", esp_err_to_name(esp_err));
        return INTERNAL_UPDATE_ERROR;
      }
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
      esp_ota_abort(otaHandle);
      return WRONG_DEVICE_ERROR;
    }
    LOG_ERR("OTA", "Firmware install failed (%s)",
            failure == ota_retry::Failure::FLASH_WRITE ? "flash write" : "download");
    esp_ota_abort(otaHandle);
    return failure == ota_retry::Failure::FLASH_WRITE ? INTERNAL_UPDATE_ERROR : HTTP_ERROR;
  }

  esp_err = esp_ota_end(otaHandle);  // verifies the written image
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_end failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  esp_err = esp_ota_set_boot_partition(updatePartition);
  if (esp_err != ESP_OK) {
    LOG_ERR("OTA", "esp_ota_set_boot_partition failed: %s", esp_err_to_name(esp_err));
    return INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
