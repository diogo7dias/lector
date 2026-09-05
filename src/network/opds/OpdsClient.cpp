#include "OpdsClient.h"

#include <Arduino.h>
#include <HalStorage.h>
#include <Logging.h>
#include <OpdsStream.h>

#include "CrossPointSettings.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace opds {

ClientStatus OpdsClient::fetchFeed(const OpdsServer& server, const std::string& pathOrUrl, FeedResult& outResult,
                                   const ReconnectCallback& onReconnect, bool* cancelFlag, const uint32_t timeoutMs,
                                   const int maxAttempts, std::function<void()> beforeAttempt,
                                   std::function<void()> afterAttempt, PollCancelCallback pollCancel) {
  outResult.entries.clear();
  outResult.searchTemplate.clear();
  outResult.nextPageUrl.clear();
  outResult.prevPageUrl.clear();
  outResult.truncated = false;

  if (server.url.empty()) return ClientStatus::NO_URL;

  const std::string url = (pathOrUrl.rfind("http://", 0) == 0 || pathOrUrl.rfind("https://", 0) == 0)
                              ? pathOrUrl
                              : UrlUtils::buildUrl(server.url, pathOrUrl);

  LOG_DBG("OPDSCLI", "Fetching: %s", url.c_str());

  auto isCancelled = [&cancelFlag, &pollCancel]() {
    if (cancelFlag && *cancelFlag) return true;
    if (pollCancel && pollCancel()) {
      if (cancelFlag) *cancelFlag = true;
      return true;
    }
    return false;
  };

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (isCancelled()) return ClientStatus::ABORTED;

#if defined(ESP32)
    // Pre-fetch heap gate: a TLS fetch on a fragmented heap risks aborting.
    if (ESP.getMaxAllocHeap() < 20 * 1024) {
      LOG_ERR("OPDSCLI", "Heap too fragmented for a fetch (largest=%u)", (unsigned)ESP.getMaxAllocHeap());
      return ClientStatus::HEAP_LOW;
    }
#endif

    OpdsParser parser;
    bool fetched = false;
    HttpDownloader::DownloadError fetchError = HttpDownloader::OK;
    int fetchStatus = 0;

    if (beforeAttempt) beforeAttempt();
    {
      OpdsParserStream stream{parser};
      fetched =
          HttpDownloader::fetchUrl(url, stream, server.username, server.password, &fetchError, &fetchStatus, timeoutMs);
    }
    if (afterAttempt) afterAttempt();

    if (fetchError == HttpDownloader::ABORTED || isCancelled()) {
      return ClientStatus::ABORTED;
    }

    if (fetched && parser) {
      outResult.searchTemplate = parser.getSearchTemplate();
      outResult.nextPageUrl = parser.getNextPageUrl();
      outResult.prevPageUrl = parser.getPrevPageUrl();
      outResult.truncated = parser.truncated();
      outResult.entries = std::move(parser).getEntries();
      return outResult.entries.empty() ? ClientStatus::EMPTY : ClientStatus::OK;
    }

    if (fetchStatus == 401) {
      LOG_ERR("OPDSCLI", "Server refused credentials (401)");
      return ClientStatus::BAD_CREDENTIALS;
    }

    if (!opds_retry::shouldRetry(fetchStatus, attempt, maxAttempts)) {
      LOG_ERR("OPDSCLI", "Fetch failed (attempt %d/%d, status %d, error %d)", attempt, maxAttempts, fetchStatus,
              static_cast<int>(fetchError));
      return (fetched && !parser) ? ClientStatus::PARSE_FAILED : ClientStatus::FETCH_FAILED;
    }

    const unsigned long delayMs = opds_retry::backoffMs(attempt);
    LOG_INF("OPDSCLI", "Fetch failed (status %d, error %d). Retrying in %lu ms (attempt %d/%d)", fetchStatus,
            static_cast<int>(fetchError), delayMs, attempt, maxAttempts);

    if (onReconnect) {
      ReconnectInfo info;
      info.attempt = attempt;
      info.maxAttempts = maxAttempts;
      info.backoffMs = delayMs;
      info.httpStatus = fetchStatus;
      info.reason = (fetchStatus == 502 || fetchStatus == 503 || fetchStatus == 504)
                        ? "Server restarting"
                        : (fetchStatus == 408 ? "Connection timeout" : "Server unavailable");
      onReconnect(info);
    }

    const unsigned long start = millis();
    while (millis() - start < delayMs) {
      if (isCancelled()) return ClientStatus::ABORTED;
      delay(20);
    }
  }

  return ClientStatus::FETCH_FAILED;
}

HttpDownloader::DownloadError OpdsClient::downloadBook(
    const OpdsServer& server, const std::string& feedUrl, const OpdsEntry& entry, std::string& outFinalPath,
    const HttpDownloader::ProgressCallback& progress, bool* cancelFlag, const ReconnectCallback& onReconnect,
    const uint32_t timeoutMs, const int maxAttempts, std::function<void()> beforeAttempt,
    std::function<void()> afterAttempt, PollCancelCallback pollCancel) {
  const std::string& base = feedUrl.empty() ? server.url : feedUrl;
  const std::string downloadUrl = UrlUtils::buildUrl(base, entry.href);

  const char* folder = SETTINGS.opdsDownloadFolder;
  bool haveFolder = folder[0] != '\0';
  if (haveFolder && !Storage.exists(folder) && !Storage.mkdir(folder)) {
    LOG_ERR("OPDSCLI", "mkdir failed for %s, using SD root", folder);
    haveFolder = false;
  }

  std::string filename;
  filename.reserve(96);
  if (haveFolder) filename += folder;
  filename += '/';
  filename += opdsBookFilename(entry.author, entry.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  LOG_DBG("OPDSCLI", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  auto isCancelled = [&cancelFlag, &pollCancel]() {
    if (cancelFlag && *cancelFlag) return true;
    if (pollCancel && pollCancel()) {
      if (cancelFlag) *cancelFlag = true;
      return true;
    }
    return false;
  };

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (isCancelled()) return HttpDownloader::ABORTED;

    std::string serverFilename;
    HttpDownloader::DownloadError result;

    if (beforeAttempt) beforeAttempt();
    result = HttpDownloader::downloadToFile(
        downloadUrl, filename, progress, cancelFlag, server.username, server.password,
        /*allowResume=*/true, server.keepFilename ? &serverFilename : nullptr, timeoutMs);
    if (afterAttempt) afterAttempt();

    if (result == HttpDownloader::OK) {
      const std::string sanitized = StringUtils::sanitizeFilename(serverFilename);
      if (server.keepFilename && !sanitized.empty()) {
        std::string finalPath;
        finalPath.reserve(96);
        if (haveFolder) finalPath += folder;
        finalPath += '/';
        finalPath += sanitized;
        if (finalPath != filename) {
          if (Storage.exists(finalPath.c_str())) Storage.remove(finalPath.c_str());
          if (Storage.rename(filename.c_str(), finalPath.c_str())) {
            filename = finalPath;
          } else {
            LOG_ERR("OPDSCLI", "rename to server filename failed, keeping %s", filename.c_str());
          }
        }
      }
      clearBookCache(filename);
      outFinalPath = filename;
      return HttpDownloader::OK;
    }

    if (result == HttpDownloader::ABORTED || isCancelled()) {
      return HttpDownloader::ABORTED;
    }

    // Only retry on network/server failures; file errors on SD are not retried
    if (result != HttpDownloader::HTTP_ERROR || attempt >= maxAttempts) {
      LOG_ERR("OPDSCLI", "Download failed permanently (attempt %d/%d, error %d)", attempt, maxAttempts,
              static_cast<int>(result));
      return result;
    }

    const unsigned long delayMs = opds_retry::backoffMs(attempt);
    LOG_INF("OPDSCLI", "Download dropped/failed. Retrying in %lu ms (attempt %d/%d)", delayMs, attempt, maxAttempts);

    if (onReconnect) {
      ReconnectInfo info;
      info.attempt = attempt;
      info.maxAttempts = maxAttempts;
      info.backoffMs = delayMs;
      info.httpStatus = 0;
      info.reason = "Server unavailable";
      onReconnect(info);
    }

    const unsigned long start = millis();
    while (millis() - start < delayMs) {
      if (isCancelled()) return HttpDownloader::ABORTED;
      delay(20);
    }
  }

  return HttpDownloader::HTTP_ERROR;
}

}  // namespace opds
