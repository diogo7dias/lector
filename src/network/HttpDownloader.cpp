#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>

#include <functional>
#include <string>

#include "HttpRangePolicy.h"
#include "WifiFullPower.h"

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>

extern "C" void wolfSSL_Arduino_Serial_Print(const char* const msg) { LOG_DBG("WOLFSSL", "%s", msg); }
#else
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#endif

namespace {
#if !defined(FREEINK_NET_WOLFSSL)
// RX holds the response headers. Smaller buffers leave enough contiguous heap
// for mbedTLS on redirect-heavy OPDS feeds while still preserving the headers
// we read directly (Location, Content-Length).
constexpr int HTTP_RX_BUF = 2048;
constexpr int HTTP_TX_BUF = 512;
#endif
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room. esp_http_client's timeout_ms is uint32, so unlike Arduino
// HTTPClient's uint16 setTimeout it doesn't silently truncate.
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr size_t READ_CHUNK = 1024;
constexpr int MAX_REDIRECTS = 5;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  // Byte offset a resumed transfer asks for with a Range header, and the bytes
  // already on disk that `downloaded` therefore starts at. 0 for a plain fetch.
  size_t rangeStart = 0;
  // When set, receives the final response's Content-Disposition header, so a
  // caller can name a file the URL did not name. Left untouched when the server
  // sends no such header.
  std::string* contentDisposition = nullptr;
  // Status of the final response, 0 when the request never got one. A caller that
  // shows an error to the reader needs to tell "the server said no" from "the
  // connection never happened".
  int status = 0;
  // Called when a ranged request came back 200 (whole body) rather than 206:
  // the partial the range was based on is worthless and must be thrown away
  // before the first chunk lands. Cleared rangeStart follows.
  std::function<bool()> restart;
};

// Applies http_range's reading of the response to the sink: throws away the
// partial when the server ignored the Range, then records the total size.
// Returns false when the partial could not be discarded.
bool beginBody(Sink& sink, int status, bool hasLength, size_t contentLength) {
  const http_range::BodyStart plan = http_range::planBodyStart(status, sink.rangeStart, hasLength, contentLength);
  if (plan.discardPartial) {
    if (sink.restart && !sink.restart()) return false;
    sink.rangeStart = 0;
  }
  sink.downloaded = plan.writeOffset;
  sink.total = plan.total;
  return true;
}

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

#if defined(FREEINK_NET_WOLFSSL)
HttpDownloader::DownloadError runGetWolf(const std::string& startUrl, const std::string& username,
                                         const std::string& password, Sink& sink) {
  std::string url = startUrl;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setInsecure();
    if (sink.contentDisposition) {
      // The default retained set is deliberately small (holding every header of a
      // CDN response has exhausted the heap before), so ask for this one by name
      // alongside the ones the client itself needs.
      http.setRetainedHeaders({"location", "content-length", "transfer-encoding", "connection", "www-authenticate",
                               "retry-after", "content-disposition"});
    }
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL bad URL: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    // setUserAgent replaces SecureHttpClient's built-in UA; addHeader would
    // append a second User-Agent header, which strict servers reject (aiohttp
    // answers 400 "Duplicate 'User-Agent' header found").
    http.setUserAgent("CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (!username.empty() && !password.empty()) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    if (sink.rangeStart > 0) {
      http.addHeader("Range", "bytes=" + std::to_string(sink.rangeStart) + "-");
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", url.c_str());
    bool bodyStarted = false;
    bool restartFailed = false;
    const int status = http.GET(
        [&http, &sink, &bodyStarted, &restartFailed](const uint8_t* data, size_t len) {
          const int code = http.getStatus();
          // Redirect bodies are drained through here too; only the final
          // response carries bytes worth keeping.
          if (!http_range::isBodyStatus(code, sink.rangeStart)) return true;
          if (!bodyStarted) {
            bodyStarted = true;
            if (!beginBody(sink, code, http.hasContentLength(), http.getContentLength())) {
              restartFailed = true;
              return false;
            }
          }
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          // Unconditional: a chunked response has no Content-Length, so gating on
          // total would leave a caller that drives the UI (or pumps a server)
          // from this callback with nothing to run for the whole transfer.
          // Callers read total == 0 as "size unknown".
          if (sink.progress) sink.progress(sink.downloaded, sink.total);
          return true;
        },
        [&sink]() { return sink.cancelFlag && *sink.cancelFlag; });

    if (restartFailed) return HttpDownloader::FILE_ERROR;

    if (http.aborted()) return HttpDownloader::ABORTED;
    sink.status = status;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed: %s (free %d bytes, largest block %d bytes)", url.c_str(),
              ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      return HttpDownloader::HTTP_ERROR;
    }
    if (isRedirect(status)) {
      const std::string location = http.getHeader("location");
      if (location.empty() || !freeink::SecureHttpClient::resolveUrl(url, location, url)) {
        LOG_ERR("HTTP", "wolfSSL bad redirect: %d", status);
        return HttpDownloader::HTTP_ERROR;
      }
      continue;
    }
    // 416 answers a Range that starts at or past the end of the file: every byte
    // asked for is already on the card. Whether those bytes are the right ones is
    // the caller's checksum to make.
    if (http_range::isRangeAlreadyComplete(status, sink.rangeStart)) return HttpDownloader::OK;
    if (!http_range::isBodyStatus(status, sink.rangeStart)) {
      LOG_ERR("HTTP", "wolfSSL unexpected status: %d for %s", status, url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    if (http.callbackAborted()) return HttpDownloader::FILE_ERROR;
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes (free %d bytes, largest block %d bytes)",
              sink.downloaded, sink.total, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      return HttpDownloader::HTTP_ERROR;
    }
    if (sink.contentDisposition) *sink.contentDisposition = http.getHeader("content-disposition");
    return HttpDownloader::OK;
  }
  LOG_ERR("HTTP", "too many redirects");
  return HttpDownloader::HTTP_ERROR;
}
#endif

#if !defined(FREEINK_NET_WOLFSSL)
// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink) {
  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.buffer_size = HTTP_RX_BUF;
  config.buffer_size_tx = HTTP_TX_BUF;
  config.timeout_ms = HTTP_TIMEOUT_MS;
  // Verify HTTPS against the bundled CA roots. This build has esp-tls
  // CONFIG_ESP_TLS_INSECURE off, so an unverified TLS handshake can't be set
  // up at all; the model is public servers over verified https and local
  // servers over plain http (esp_http_client picks the transport from the URL
  // scheme, so http:// needs no cert config). The prior setInsecure() worked
  // only because Arduino's ssl_client drives mbedtls directly.
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.keep_alive_enable = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
  if (sink.rangeStart > 0) {
    const std::string range = "bytes=" + std::to_string(sink.rangeStart) + "-";
    esp_http_client_set_header(client, "Range", range.c_str());
  }
  if (!username.empty() && !password.empty()) {
    // Preemptive Basic auth, like the prior addHeader; don't wait for a 401.
    const std::string credentials = username + ":" + password;
    const String header = "Basic " + base64::encode(credentials.c_str());
    esp_http_client_set_header(client, "Authorization", header.c_str());
  }

  // open()/read() does not auto-follow redirects (only perform() does), so step
  // 30x responses manually. OPDS download endpoints and the GitHub release CDN
  // both redirect.
  esp_err_t err = esp_http_client_open(client, 0);
  if (err != ESP_OK) {
    LOG_ERR("HTTP", "open failed: %s", esp_err_to_name(err));
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }
  int64_t contentLength = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  for (int hop = 0; isRedirect(status) && hop < MAX_REDIRECTS; ++hop) {
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    esp_http_client_close(client);
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
  }

  // 416 answers a Range that starts at or past the end of the file: every byte
  // asked for is already on the card. Whether those bytes are the right ones is
  // the caller's checksum to make.
  if (http_range::isRangeAlreadyComplete(status, sink.rangeStart)) {
    esp_http_client_cleanup(client);
    return HttpDownloader::OK;
  }
  if (!http_range::isBodyStatus(status, sink.rangeStart)) {
    LOG_ERR("HTTP", "unexpected status: %d", status);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  // sink.contentDisposition is deliberately left untouched here. This transport
  // has no way to read a *response* header without an HTTP_EVENT_ON_HEADER event
  // handler (esp_http_client_get_header reads back request headers), and every
  // current build uses the wolfSSL path, which does report it. Callers already
  // treat an empty result as "the server offered no name".

  // fetch_headers returns 0 for a chunked response (no Content-Length); leave
  // total at 0 so progress stays silent and the size check is skipped.
  if (!beginBody(sink, status, contentLength > 0, static_cast<size_t>(contentLength > 0 ? contentLength : 0))) {
    esp_http_client_cleanup(client);
    return HttpDownloader::FILE_ERROR;
  }

  auto buf = makeUniqueNoThrow<char[]>(READ_CHUNK);
  if (!buf) {
    LOG_ERR("HTTP", "OOM: %u byte read buffer", (unsigned)READ_CHUNK);
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  while (true) {
    if (sink.cancelFlag && *sink.cancelFlag) {
      esp_http_client_cleanup(client);
      return HttpDownloader::ABORTED;
    }
    const int read = esp_http_client_read(client, buf.get(), READ_CHUNK);
    if (read < 0) {
      LOG_ERR("HTTP", "read error after %zu bytes", sink.downloaded);
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    if (sink.progress) sink.progress(sink.downloaded, sink.total);  // see runGetWolf: total == 0 means unknown
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  esp_http_client_cleanup(client);
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif  // !FREEINK_NET_WOLFSSL

// All HTTP(S) fetches go through wolfSSL when it is the active TLS stack: it
// speaks TLS 1.3 and reads large bodies from servers where the esp_http_client/
// mbedTLS path fails to connect or stalls mid-stream. Plain-http URLs still use a
// WiFiClient inside runGetWolf, so this is safe for non-TLS targets too.
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password, Sink& sink) {
#if defined(FREEINK_NET_WOLFSSL)
  return runGetWolf(url, username, password, sink);
#else
  return runGet(url, username, password, sink);
#endif
}
}  // namespace

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password, DownloadError* outError, int* outStatus) {
  LOG_DBG("HTTP", "Fetching: %s (free %d bytes, largest block %d bytes)", url.c_str(), ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
  const WifiFullPower fullPower;
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  const DownloadError result = runGetSecure(url, username, password, sink);
  if (outError) *outError = result;
  if (outStatus) *outStatus = sink.status;
  return result == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  const WifiFullPower fullPower;
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password, const size_t rangeStart, bool* resumedFromStart) {
  LOG_DBG("HTTP", "Fetching: %s (from %zu)", url.c_str(), rangeStart);
  const WifiFullPower fullPower;
  Sink sink;
  sink.write = onData;
  sink.rangeStart = rangeStart;
  // A server that answers a Range with the whole body replays bytes the caller
  // already has. There is no file to throw away here, so the caller is told and
  // starts over; a caller that passed no flag cannot, and the transfer aborts.
  if (rangeStart > 0) {
    sink.restart = [resumedFromStart]() {
      if (!resumedFromStart) return false;
      *resumedFromStart = true;
      return true;
    };
  }
  return runGetSecure(url, username, password, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             const bool allowResume, std::string* contentDisposition) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  const WifiFullPower fullPower;

  // Bytes from an earlier attempt that this one can carry on from. Without
  // allowResume any leftover is stale by definition and goes.
  size_t resumeFrom = 0;
  if (allowResume && Storage.exists(destPath.c_str())) {
    HalFile existing;
    if (Storage.openFileForRead("HTTP", destPath.c_str(), existing)) {
      resumeFrom = existing.fileSize();
      existing.close();
    }
  } else if (!allowResume && Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }

  HalFile file;
  if (resumeFrom > 0) {
    // Positioned, not appending: a server that ignores the Range makes the body
    // start at 0 again, and O_APPEND would send those bytes to the end of the
    // partial instead of over it.
    file = Storage.open(destPath.c_str(), O_WRONLY | O_CREAT);
    if (!file.isOpen() || !file.seekSet(resumeFrom)) {
      LOG_ERR("HTTP", "Failed to open file for append");
      return FILE_ERROR;
    }
    LOG_DBG("HTTP", "Resuming at %zu bytes", resumeFrom);
  } else if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.rangeStart = resumeFrom;
  sink.downloaded = resumeFrom;
  sink.contentDisposition = contentDisposition;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };
  // The server answered a ranged request with the whole body, so the bytes about
  // to arrive start at 0 rather than continuing the partial.
  //
  // The partial is rewound, not deleted. It holds the head of this same file, and
  // the body now arriving is that same content from byte 0, so the bytes past
  // wherever this attempt stops are still the right ones. Truncating instead threw
  // 32768 staged bytes away every time the CDN ignored a Range, which it does
  // often enough that a file that needed several attempts never converged: each
  // round carried 15 KB and then lost 32 KB. A wrong guess here cannot install a
  // bad font; the caller checksums the finished file and starts clean on
  // a mismatch.
  sink.restart = [&file, &destPath]() {
    if (!file.seekSet(0)) {
      LOG_ERR("HTTP", "Failed to rewind the partial after an ignored Range");
      return false;
    }
    return true;
  };

  const DownloadError result = runGetSecure(url, username, password, sink);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  file.close();

  if (result != OK) {
    // Only bytes that actually arrived are worth keeping for the next attempt.
    // A failure that never reached the body (bad URL, 404, a redirect that went
    // nowhere) leaves an empty file that would otherwise be resumed from 0 and
    // read as a partial, so it is swept up here along with every non-resumable
    // caller's leftovers.
    // Bytes on the card, not bytes this attempt wrote: an attempt that was rewound
    // by an ignored Range and then died early still leaves the longer partial an
    // earlier attempt staged, and that is worth keeping.
    size_t staged = 0;
    if (HalFile existing; Storage.openFileForRead("HTTP", destPath.c_str(), existing)) {
      staged = existing.fileSize();
      existing.close();
    }
    if (!allowResume || staged == 0) Storage.remove(destPath.c_str());
    return result;
  }
  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  // Belt and braces over the transport's own completeness check: a short body that
  // still reports complete would leave a truncated book on the SD card, which then
  // fails much later and looks like a corrupt file rather than a bad download.
  // sink.total stays 0 for chunked responses, where no length was promised.
  if (sink.total > 0 && sink.downloaded != sink.total) {
    LOG_ERR("HTTP", "short download: got %zu of %zu bytes", sink.downloaded, sink.total);
    if (!allowResume) Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);
  return OK;
}
