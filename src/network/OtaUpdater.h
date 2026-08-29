#pragma once

#include <string>

class OtaUpdater {
  bool updateAvailable = false;
  std::string latestVersion;
  std::string otaUrl;
  size_t otaSize = 0;
  size_t processedSize = 0;
  size_t totalSize = 0;

 public:
  using ProgressCallback = void (*)(void* ctx);

  enum OtaUpdaterError {
    OK = 0,
    NO_UPDATE,
    HTTP_ERROR,
    JSON_PARSE_ERROR,
    UPDATE_OLDER_ERROR,
    INTERNAL_UPDATE_ERROR,
    OOM_ERROR,
    WRONG_DEVICE_ERROR,
  };

  size_t getOtaSize() const { return otaSize; }

  size_t getProcessedSize() const { return processedSize; }

  size_t getTotalSize() const { return totalSize; }

  OtaUpdater() = default;
  bool isUpdateNewer() const;
  const std::string& getLatestVersion() const;
  OtaUpdaterError checkForUpdate();

  // True when the firmware itself would be fetched over TLS. The OTA Unlocker
  // used to get off a USB-locked device serves the bytes over plain HTTP on its
  // own bridge, which costs no heap; a plain GitHub release does not.
  bool isDownloadSecure() const;

  // `allowAnyVersion` installs whatever the server offers, including the same
  // version or an older one, and including another firmware entirely. It is how
  // a user leaves lector on a device whose USB flashing the vendor locked.
  // Check for Updates leaves it false and still refuses anything not newer.
  OtaUpdaterError installUpdate(ProgressCallback onProgress = nullptr, void* ctx = nullptr,
                                bool allowAnyVersion = false);
};
