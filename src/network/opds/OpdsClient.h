#pragma once

#include <OpdsParser.h>

#include <functional>
#include <string>
#include <vector>

#include "OpdsServerStore.h"
#include "network/HttpDownloader.h"
#include "network/opds/OpdsRetryPolicy.h"

namespace opds {

enum class ClientStatus { OK = 0, NO_URL, HEAP_LOW, FETCH_FAILED, PARSE_FAILED, EMPTY, BAD_CREDENTIALS, ABORTED };

struct FeedResult {
  std::vector<OpdsEntry> entries;
  std::string searchTemplate;
  std::string nextPageUrl;
  std::string prevPageUrl;
  bool truncated = false;
};

// Carries state for UI notifications when reconnecting after a failure
struct ReconnectInfo {
  int attempt = 0;
  int maxAttempts = 0;
  unsigned long backoffMs = 0;
  int httpStatus = 0;
  const char* reason = nullptr;
};

using ReconnectCallback = std::function<void(const ReconnectInfo& info)>;
using PollCancelCallback = std::function<bool()>;

class OpdsClient {
 public:
  OpdsClient() = default;

  /**
   * Fetch and parse an OPDS feed with exponential backoff and reconnection logic.
   *
   * When the OPDS server is rebooting or temporarily unavailable, retries up to
   * maxAttempts with exponential backoff. onReconnect is called before each retry
   * to allow the caller to update UI indicators. beforeAttempt and afterAttempt allow
   * scoping resources such as framebuffer loans per individual attempt.
   */
  ClientStatus fetchFeed(const OpdsServer& server, const std::string& pathOrUrl, FeedResult& outResult,
                         const ReconnectCallback& onReconnect = nullptr, bool* cancelFlag = nullptr,
                         uint32_t timeoutMs = opds_retry::DEFAULT_TIMEOUT_MS,
                         int maxAttempts = opds_retry::MAX_ATTEMPTS, std::function<void()> beforeAttempt = nullptr,
                         std::function<void()> afterAttempt = nullptr, PollCancelCallback pollCancel = nullptr);

  /**
   * Download a book EPUB with exponential backoff and resume support.
   *
   * If the connection drops or the server reboots during download, retries with
   * exponential backoff, resuming from bytes already saved to SD.
   */
  HttpDownloader::DownloadError downloadBook(
      const OpdsServer& server, const std::string& feedUrl, const OpdsEntry& entry, std::string& outFinalPath,
      const HttpDownloader::ProgressCallback& progress = nullptr, bool* cancelFlag = nullptr,
      const ReconnectCallback& onReconnect = nullptr, uint32_t timeoutMs = opds_retry::DEFAULT_TIMEOUT_MS,
      int maxAttempts = opds_retry::MAX_ATTEMPTS, std::function<void()> beforeAttempt = nullptr,
      std::function<void()> afterAttempt = nullptr, PollCancelCallback pollCancel = nullptr);
};

}  // namespace opds
