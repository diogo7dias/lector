#include "OpdsClient.h"

#include <Arduino.h>
#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <OpdsAuth.h>
#include <OpdsStream.h>

#include "CrossPointSettings.h"
#include "OpdsTokenStore.h"
#include "network/FetchUrlPolicy.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace opds {
namespace {
// The stored password only reaches the server it was entered for, never a host a
// feed link points at.
const std::string& ifSameServer(const OpdsServer& server, const std::string& url, const std::string& secret) {
  static const std::string none;
  return fetch_url::mayCarryCredentials(server.url, url) ? secret : none;
}

// The same rule for the server's custom headers and its bearer token: a feed link that
// points at another host (a CDN, another catalogue) gets none of them. HttpDownloader
// repeats the check on every redirect hop. A 401 answer's body is kept for signIn.
HttpDownloader::RequestExtras extrasFor(const OpdsServer& server, const std::string& url, const std::string& bearer,
                                        std::string* challenge, bool* otherOrigin = nullptr) {
  HttpDownloader::RequestExtras extras;
  if (fetch_url::mayCarryCredentials(server.url, url)) {
    extras.headers = server.activeCustomHeaders();
    extras.bearer = bearer;
  }
  extras.challengeBody = challenge;
  extras.answeredByOtherOrigin = otherOrigin;
  return extras;
}

// One token request: a form posted to the server's own token endpoint.
bool requestToken(const std::string& tokenUrl, const std::string& form, const OpdsServer& server,
                  opds_auth::TokenResponse& out, const uint32_t timeoutMs) {
  HttpDownloader::RequestExtras extras;
  extras.headers = server.activeCustomHeaders();
  std::string response;
  int status = 0;
  const bool posted = HttpDownloader::postForm(tokenUrl, form, response, status, extras, timeoutMs);
  if (!posted || !opds_auth::parseTokenResponse(response.data(), response.size(), out)) {
    LOG_ERR("OPDSCLI", "Token request failed (status %d)", status);
    return false;
  }
  return true;
}

// Answers a 401. A server whose authentication document offers the OAuth password flow gets
// a bearer token: renewed with the stored refresh token when there is one, else from the
// stored username and password. True when `bearer` holds a token worth one more request.
// The password and the refresh token go only to the server's own host, token endpoint
// included; a document that names another host is refused, not followed.
bool signIn(const OpdsServer& server, const std::string& url, const std::string& challenge, std::string& bearer,
            const uint32_t timeoutMs) {
  if (!fetch_url::mayCarryCredentials(server.url, url)) return false;
  opds_auth::AuthDocument doc;
  if (!opds_auth::parseAuthDocument(challenge.data(), challenge.size(), doc)) return false;  // plain Basic, or no flow
#if defined(ESP32)
  if (ESP.getMaxAllocHeap() < 20 * 1024) {
    LOG_ERR("OPDSCLI", "Heap too fragmented to sign in (largest=%u)", (unsigned)ESP.getMaxAllocHeap());
    return false;
  }
#endif
  const std::string key = OpdsTokenStore::keyFor(server.url, server.username);
  OpdsTokens tokens = OPDS_TOKENS.get(key);
  opds_auth::TokenResponse issued;

  bool signedIn = false;
  if (!bearer.empty() && !tokens.refreshToken.empty() &&
      fetch_url::mayCarryCredentials(server.url, tokens.refreshUrl)) {
    signedIn =
        requestToken(tokens.refreshUrl, opds_auth::refreshGrantBody(tokens.refreshToken), server, issued, timeoutMs);
    // A server that rotates its refresh token sends the new one; otherwise the old one stays.
    if (signedIn && issued.refreshToken.empty()) issued.refreshToken = tokens.refreshToken;
  }
  if (!signedIn) {
    if (server.username.empty() || server.password.empty()) {
      LOG_ERR("OPDSCLI", "Server wants a sign-in but no credentials are set");
      return false;
    }
    const std::string tokenUrl = UrlUtils::buildUrl(url, doc.tokenUrl);
    if (!fetch_url::mayCarryCredentials(server.url, tokenUrl)) {
      LOG_ERR("OPDSCLI", "Token endpoint is on another host; credentials not sent");
      return false;
    }
    if (!requestToken(tokenUrl, opds_auth::passwordGrantBody(server.username, server.password), server, issued,
                      timeoutMs)) {
      return false;
    }
    tokens.refreshUrl = doc.refreshUrl.empty() ? tokenUrl : UrlUtils::buildUrl(url, doc.refreshUrl);
  }
  LOG_INF("OPDSCLI", "Signed in to %s", server.name.c_str());

  tokens.accessToken = issued.accessToken;
  tokens.refreshToken = issued.refreshToken;
  if (tokens.refreshToken.empty()) tokens.refreshUrl.clear();
  if (!OPDS_TOKENS.put(key, tokens)) LOG_ERR("OPDSCLI", "Token not kept; it serves this visit only");
  bearer = tokens.accessToken;
  return true;
}
}  // namespace

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

  const std::string tokenKey = OpdsTokenStore::keyFor(server.url, server.username);
  std::string bearer = OPDS_TOKENS.get(tokenKey).accessToken;
  std::string challenge;
  bool signInTried = false;
  bool otherOrigin = false;

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

    challenge.clear();
    if (beforeAttempt) beforeAttempt();
    {
      OpdsParserStream stream{parser};
      fetched = HttpDownloader::fetchUrl(url, stream, ifSameServer(server, url, server.username),
                                         ifSameServer(server, url, server.password), &fetchError, &fetchStatus,
                                         timeoutMs, extrasFor(server, url, bearer, &challenge, &otherOrigin));
    }
    if (afterAttempt) afterAttempt();

    if (fetchError == HttpDownloader::ABORTED || isCancelled()) {
      return ClientStatus::ABORTED;
    }

    // The parser stops the transfer itself when free heap reaches its floor.
    // Retrying would only run the same feed down the same heap, so report it.
    if (parser.heapAborted()) {
      LOG_ERR("OPDSCLI", "Feed too large for free memory; stopped after %u entries", (unsigned)parser.entryCount());
      return ClientStatus::FEED_TOO_LARGE;
    }

    if (fetched && parser) {
      outResult.searchTemplate = parser.getSearchTemplate();
      outResult.nextPageUrl = parser.getNextPageUrl();
      outResult.prevPageUrl = parser.getPrevPageUrl();
      outResult.truncated = parser.truncated();
      outResult.entries = parser.takeEntries();
      return outResult.entries.empty() ? ClientStatus::EMPTY : ClientStatus::OK;
    }

    if (fetchError == HttpDownloader::CERT_UNTRUSTED) return ClientStatus::CERT_UNTRUSTED;

    if (fetchStatus == 401) {
      if (!signInTried) {
        signInTried = true;
        if (signIn(server, url, challenge, bearer, timeoutMs)) {
          --attempt;  // a new token is not a failed attempt
          continue;
        }
        if (!bearer.empty()) {
          // The stored token is dead and no sign-in replaces it: Basic is the only way left.
          OPDS_TOKENS.put(tokenKey, {});
          bearer.clear();
          --attempt;
          continue;
        }
      }
      LOG_ERR("OPDSCLI", "Server refused credentials (401%s)", otherOrigin ? ", after a redirect" : "");
      // Another origin never saw the password, so it cannot have been the wrong one.
      return otherOrigin ? ClientStatus::REDIRECTED : ClientStatus::BAD_CREDENTIALS;
    }

    if (!opds_retry::shouldRetry(fetchStatus, attempt, maxAttempts)) {
      LOG_ERR("OPDSCLI", "Fetch failed (attempt %d/%d, status %d, error %d)", attempt, maxAttempts, fetchStatus,
              static_cast<int>(fetchError));
      if (fetched && !parser) return ClientStatus::PARSE_FAILED;
      if (fetchError == HttpDownloader::NO_CONNECTION) return ClientStatus::UNREACHABLE;
      return opds_retry::isServerError(fetchStatus) ? ClientStatus::SERVER_ERROR : ClientStatus::FETCH_FAILED;
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

  std::string filename =
      opdsTargetPath(haveFolder ? folder : "", entry.author, entry.title,
                     static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat), opdsBookExtension(entry.format));
  LOG_DBG("OPDSCLI", "Downloading: %s -> %s", downloadUrl.c_str(), filename.c_str());

  auto isCancelled = [&cancelFlag, &pollCancel]() {
    if (cancelFlag && *cancelFlag) return true;
    if (pollCancel && pollCancel()) {
      if (cancelFlag) *cancelFlag = true;
      return true;
    }
    return false;
  };

  // Bytes land under ".part" and take the book's name only once complete, so a
  // failed download never shows up as a half-book in the library and an older
  // book of the same name stays intact until the new one has fully arrived.
  // Retries within this call resume the partial; giving up deletes it, so no
  // later download can append onto a different edition's bytes.
  const std::string partPath = filename + FsHelpers::PARTIAL_SUFFIX;
  auto giveUp = [&partPath](const HttpDownloader::DownloadError error) {
    Storage.remove(partPath.c_str());
    return error;
  };

  const std::string tokenKey = OpdsTokenStore::keyFor(server.url, server.username);
  std::string bearer = OPDS_TOKENS.get(tokenKey).accessToken;
  std::string challenge;
  bool signInTried = false;

  for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
    if (isCancelled()) return giveUp(HttpDownloader::ABORTED);

    std::string contentDisposition;
    HttpDownloader::DownloadError result;
    int status = 0;
    challenge.clear();

    if (beforeAttempt) beforeAttempt();
    result = HttpDownloader::downloadToFile(downloadUrl, partPath, progress, cancelFlag,
                                            ifSameServer(server, downloadUrl, server.username),
                                            ifSameServer(server, downloadUrl, server.password),
                                            /*allowResume=*/true, server.keepFilename ? &contentDisposition : nullptr,
                                            timeoutMs, &status, extrasFor(server, downloadUrl, bearer, &challenge));
    if (afterAttempt) afterAttempt();

    if (result == HttpDownloader::OK) {
      // downloadToFile hands back the raw header ("attachment; filename=..."), and
      // sanitizeFilename never returns empty: sanitizing the header itself renamed
      // every book to the header text, or to "book" when the server sent none.
      const std::string offered = fetch_url::filenameFromContentDisposition(contentDisposition);
      // The part takes the server's name in one step. Landing under the generated
      // name first and renaming again removed "whatever is at the new name" in
      // between, and on FAT a name differing only in case is the book itself.
      const auto place = [&partPath](const std::string& target) {
        if (Storage.exists(target.c_str())) Storage.remove(target.c_str());
        return Storage.rename(partPath.c_str(), target.c_str());
      };
      bool placed = false;
      if (server.keepFilename && !offered.empty()) {
        const std::string named = std::string(haveFolder ? folder : "") + '/' + offered;
        placed = place(named);
        if (placed) {
          filename = named;
        } else {
          LOG_ERR("OPDSCLI", "rename to server filename failed, keeping %s", filename.c_str());
        }
      }
      if (!placed && !place(filename)) {
        LOG_ERR("OPDSCLI", "rename of finished download failed: %s", partPath.c_str());
        return giveUp(HttpDownloader::FILE_ERROR);
      }
      clearBookCache(filename);
      outFinalPath = filename;
      return HttpDownloader::OK;
    }

    if (result == HttpDownloader::ABORTED || isCancelled()) {
      return giveUp(HttpDownloader::ABORTED);
    }

    // A token that expired between browsing and downloading: renew it and go again.
    if (status == 401 && !signInTried) {
      signInTried = true;
      if (signIn(server, downloadUrl, challenge, bearer, timeoutMs)) {
        --attempt;
        continue;
      }
    }

    // Retry only a transfer that dropped. A status the server answered with, a bad
    // URL or an SD error will fail the same way again.
    const bool dropped = result == HttpDownloader::NO_CONNECTION || result == HttpDownloader::INCOMPLETE;
    if (!dropped || attempt >= maxAttempts) {
      LOG_ERR("OPDSCLI", "Download failed permanently (attempt %d/%d, error %d)", attempt, maxAttempts,
              static_cast<int>(result));
      return giveUp(result);
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
      if (isCancelled()) return giveUp(HttpDownloader::ABORTED);
      delay(20);
    }
  }

  return HttpDownloader::HTTP_ERROR;
}

}  // namespace opds
