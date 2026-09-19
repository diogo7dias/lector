#pragma once
#include <HalStorage.h>

#include <functional>
#include <string>

/**
 * HTTP client utility for fetching content and downloading files. Built on
 * esp_http_client: https is verified against the CA bundle, plain http is
 * used for local servers (transport is chosen from the URL scheme).
 */
class HttpDownloader {
 public:
  using ProgressCallback = std::function<void(size_t downloaded, size_t total)>;
  // Called with each body chunk as it arrives; return false to abort. Lets a
  // streaming parser consume the response without buffering the whole body.
  using DataCallback = std::function<bool(const uint8_t* data, size_t len)>;

  /**
   * Why a transfer failed. HTTP_ERROR stays as the catch-all so callers that
   * only test `!= OK` are unaffected, but the transport now says which kind of
   * failure it was whenever it can tell: a reader who cannot open a serial log
   * still has to be able to tell "the router is not there" from "the server
   * said no" from "the card is full", because those need different actions.
   */
  enum DownloadError {
    OK = 0,
    HTTP_ERROR,
    FILE_ERROR,
    ABORTED,
    // TCP or TLS never came up: no HTTP response was read at all. Wrong access
    // point, no route out, DNS, or a handshake that could not complete.
    NO_CONNECTION,
    // A response arrived carrying a status this transfer cannot use (404, 403,
    // 5xx, a redirect with no usable Location, too many hops).
    SERVER_ERROR,
    // The body started and then stopped short of what was promised.
    INCOMPLETE,
  };

  static constexpr uint32_t DEFAULT_TIMEOUT_MS = 60000;

  /**
   * Fetch text content from a URL with optional credentials.
   */
  static bool fetchUrl(const std::string& url, std::string& outContent, const std::string& username = "",
                       const std::string& password = "", uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

  /**
   * As above, streaming into `stream`. outError receives why a failed fetch
   * failed, so a caller can tell the reader whether the server refused it or the
   * connection never got that far.
   */
  static bool fetchUrl(const std::string& url, Stream& stream, const std::string& username = "",
                       const std::string& password = "", DownloadError* outError = nullptr, int* outStatus = nullptr,
                       uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

  /**
   * Stream the response body to onData as it arrives, without buffering it.
   *
   * rangeStart resumes a transfer that stopped early: the request carries a
   * Range header and onData is handed only the bytes from that offset on. A
   * server that ignores the Range and answers 200 replays the whole body from
   * the start, which the caller must be able to take (see resumedFromStart).
   */
  static bool fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username = "",
                       const std::string& password = "", size_t rangeStart = 0, bool* resumedFromStart = nullptr,
                       uint32_t timeoutMs = DEFAULT_TIMEOUT_MS);

  /**
   * Download a file to the SD card with optional credentials.
   *
   * With allowResume set, a file already on the card is treated as a partial
   * download: the request carries a Range header, the body is appended, and a
   * failed transfer leaves what arrived behind so the next call can carry on
   * from there. The caller owns that leftover and must delete it once the file
   * is no longer wanted. A server that ignores the Range and answers 200 is
   * handled by discarding the partial and starting again.
   *
   * The offset a 206 was served from is not checked against what was asked for,
   * so a resumed file is only as trustworthy as the caller's own verification:
   * use allowResume for content with a checksum to test afterwards.
   */
  /**
   * `contentDisposition`, when given, receives the raw Content-Disposition header
   * of the final response, or stays untouched when the server sends none. Callers
   * that want a filename out of it parse it themselves (see FetchUrlPolicy.h);
   * the download itself always writes to `destPath`.
   *
   * `outStatus`, when given, receives the final response's HTTP status, or 0 when
   * the request never got one. A screen that has to name a failure to someone who
   * cannot read a serial log needs the number, not only the category.
   */
  static DownloadError downloadToFile(const std::string& url, const std::string& destPath,
                                      ProgressCallback progress = nullptr, bool* cancelFlag = nullptr,
                                      const std::string& username = "", const std::string& password = "",
                                      bool allowResume = false, std::string* contentDisposition = nullptr,
                                      uint32_t timeoutMs = DEFAULT_TIMEOUT_MS, int* outStatus = nullptr);
};
