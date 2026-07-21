#include "HttpDownloader.h"

#include <Arduino.h>
#include <Logging.h>
#include <Memory.h>
#include <base64.h>

#if defined(FREEINK_NET_WOLFSSL)
#include <SecureHttpClient.h>  // wolfSSL TLS 1.3 path (SecureNet). wolfSSL_Arduino_Serial_Print lives in WolfSslGlue.cpp
#else
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <strings.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace {
// Per-socket-op timeout. Some OPDS download endpoints are slow to send headers
// (>15s) and chunked catalogs stall mid-body, so 15s killed them. 60s gives
// slow servers room.
constexpr int HTTP_TIMEOUT_MS = 60000;
constexpr int MAX_REDIRECTS = 5;

#if !defined(FREEINK_NET_WOLFSSL)
// RX holds the response headers. 4096 fits real OPDS servers; GitHub's release
// CDN sends more and logs HTTP_HEADER "Buffer length is small", but that's
// non-fatal: the headers we read (Location, Content-Length) come first and
// survive. Smaller keeps contiguous heap free while WiFi and TLS are up. TX
// only carries our GET; the body streams in READ_CHUNK pieces.
constexpr int HTTP_RX_BUF = 4096;
constexpr int HTTP_TX_BUF = 1024;
constexpr size_t READ_CHUNK = 2048;
#endif

// See HttpDownloader::lastHttpStatus(). 0 = no response received.
int s_lastHttpStatus = 0;

struct Sink {
  std::function<bool(const uint8_t*, size_t)> write;  // returns false to abort the transfer
  HttpDownloader::ProgressCallback progress;
  bool* cancelFlag = nullptr;
  size_t total = 0;
  size_t downloaded = 0;
  std::string contentDisposition;  // captured Content-Disposition header (upstream #2415)
};

#if !defined(FREEINK_NET_WOLFSSL)
// Capture the Content-Disposition response header so the caller can honor the
// server-provided filename. esp_http_client delivers headers via this callback.
esp_err_t httpEventHandler(esp_http_client_event_t* evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(evt->header_key, "Content-Disposition") == 0) {
    if (auto* sink = static_cast<Sink*>(evt->user_data)) {
      sink->contentDisposition = evt->header_value;
    }
  }
  return ESP_OK;
}
#endif

std::string urlDecode(const std::string& str) {
  std::string res;
  res.reserve(str.size());
  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == '%' && i + 2 < str.size()) {
      const unsigned char c1 = static_cast<unsigned char>(str[i + 1]);
      const unsigned char c2 = static_cast<unsigned char>(str[i + 2]);
      if (std::isxdigit(c1) && std::isxdigit(c2)) {
        const char hex[3] = {str[i + 1], str[i + 2], '\0'};
        res += static_cast<char>(strtol(hex, nullptr, 16));
        i += 2;
      } else {
        res += str[i];
      }
    } else {
      res += str[i];
    }
  }
  return res;
}

// Extract the filename from a Content-Disposition header. Handles quoted/bare
// filename= and RFC 5987 filename*=UTF-8''... (percent-decoded). Upstream #2415.
std::string parseContentDisposition(const std::string& header) {
  std::string lowerHeader = header;
  std::transform(lowerHeader.begin(), lowerHeader.end(), lowerHeader.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  bool isRfc5987 = false;
  size_t pos = lowerHeader.find("filename*=");
  if (pos != std::string::npos) {
    isRfc5987 = true;
    pos += 10;
  } else {
    pos = lowerHeader.find("filename=");
    if (pos == std::string::npos) return "";
    pos += 9;
  }

  std::string fn = header.substr(pos);
  while (!fn.empty() && std::isspace(static_cast<unsigned char>(fn.front()))) fn.erase(fn.begin());
  while (!fn.empty() && std::isspace(static_cast<unsigned char>(fn.back()))) fn.pop_back();

  if (!fn.empty() && fn.front() == '"') {
    fn = fn.substr(1);
    const size_t q = fn.find('"');
    if (q != std::string::npos) fn.resize(q);
  } else if (!fn.empty()) {
    const size_t space = fn.find_first_of("; \t\r\n");
    if (space != std::string::npos) fn.resize(space);
  }

  if (isRfc5987) {
    const size_t firstQuote = fn.find('\'');
    if (firstQuote != std::string::npos) {
      const size_t secondQuote = fn.find('\'', firstQuote + 1);
      fn = (secondQuote != std::string::npos) ? fn.substr(secondQuote + 1) : fn.substr(firstQuote + 1);
    }
    return urlDecode(fn);
  }
  return fn;
}

bool isRedirect(int status) {
  return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

#if !defined(FREEINK_NET_WOLFSSL)
// Streams a GET body through sink.write in READ_CHUNK pieces. Uses the manual
// open/fetch_headers/read path rather than esp_http_client_perform(): perform()
// pushes the whole body through an event callback and reports a chunked body
// that ends early as ESP_ERR_HTTP_INCOMPLETE_DATA, whereas the read loop streams
// large/slow files and surfaces a short read directly.
HttpDownloader::DownloadError runGet(const std::string& url, const std::string& username, const std::string& password,
                                     Sink& sink) {
  s_lastHttpStatus = 0;
  // Diagnostic line for cable debug sessions: never logs the credentials
  // themselves, but userLen/passLen expose the classic stale-password case
  // (device password out of sync with the server after a server-side change).
  LOG_INF("HTTP", "GET %s auth=%s userLen=%u passLen=%u", url.c_str(),
          (!username.empty() && !password.empty()) ? "basic" : "none", static_cast<unsigned>(username.size()),
          static_cast<unsigned>(password.size()));
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
  config.event_handler = httpEventHandler;  // capture Content-Disposition (#2415)
  config.user_data = &sink;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    LOG_ERR("HTTP", "client init failed");
    return HttpDownloader::HTTP_ERROR;
  }

  esp_http_client_set_header(client, "User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
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
  sink.contentDisposition.clear();
  int64_t contentLength = esp_http_client_fetch_headers(client);
  int status = esp_http_client_get_status_code(client);
  int hops = 0;
  for (; isRedirect(status) && hops < 5; ++hops) {
    LOG_INF("HTTP", "redirect %d, following (hop %d)", status, hops + 1);
    if (esp_http_client_set_redirection(client) != ESP_OK) break;
    err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
      LOG_ERR("HTTP", "redirect open failed: %s", esp_err_to_name(err));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    sink.contentDisposition.clear();  // only keep the final response's header (#2415)
    contentLength = esp_http_client_fetch_headers(client);
    status = esp_http_client_get_status_code(client);
  }

  s_lastHttpStatus = status;
  LOG_INF("HTTP", "status=%d contentLength=%lld hops=%d", status, static_cast<long long>(contentLength), hops);
  if (status != 200) {
    if (status == 401 || status == 403) {
      LOG_ERR("HTTP", "auth rejected (%d): device credentials likely stale, retype the server password", status);
    } else {
      LOG_ERR("HTTP", "unexpected status: %d", status);
    }
    esp_http_client_cleanup(client);
    return HttpDownloader::HTTP_ERROR;
  }

  // fetch_headers returns 0 for a chunked response (no Content-Length); leave
  // total at 0 so progress stays silent and the size check is skipped.
  sink.total = contentLength > 0 ? static_cast<size_t>(contentLength) : 0;

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
      LOG_ERR("HTTP", "read error after %zu bytes (errno %d)", sink.downloaded, esp_http_client_get_errno(client));
      esp_http_client_cleanup(client);
      return HttpDownloader::HTTP_ERROR;
    }
    if (read == 0) break;  // all data received
    if (!sink.write(reinterpret_cast<const uint8_t*>(buf.get()), read)) {
      esp_http_client_cleanup(client);
      return HttpDownloader::FILE_ERROR;
    }
    sink.downloaded += read;
    if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
  }

  const bool complete = esp_http_client_is_complete_data_received(client);
  // Body size is a strong parse-failure clue: a ~1KB "200 OK" body on a feed
  // URL is usually an HTML login page, not an OPDS XML document.
  LOG_INF("HTTP", "body done: %zu bytes, complete=%d", sink.downloaded, complete ? 1 : 0);
  esp_http_client_cleanup(client);
  if (!complete) {
    LOG_ERR("HTTP", "incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
    return HttpDownloader::HTTP_ERROR;
  }
  return HttpDownloader::OK;
}
#endif  // !FREEINK_NET_WOLFSSL

#if defined(FREEINK_NET_WOLFSSL)
// Streams a GET body over wolfSSL (SecureNet). Manual redirect loop (SecureHttpClient
// returns 30x to the caller); the body is streamed via the GET data callback so a
// large download never buffers whole. Captures Content-Disposition for #2415.
HttpDownloader::DownloadError runGetWolf(const std::string& startUrl, const std::string& username,
                                         const std::string& password, Sink& sink) {
  s_lastHttpStatus = 0;
  std::string url = startUrl;

  for (int hop = 0; hop <= MAX_REDIRECTS; ++hop) {
    freeink::SecureHttpClient http;
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.setInsecure();
    if (!http.begin(url)) {
      LOG_ERR("HTTP", "wolfSSL bad URL: %s", url.c_str());
      return HttpDownloader::HTTP_ERROR;
    }
    http.addHeader("User-Agent", "CrossPoint-ESP32-" CROSSPOINT_VERSION);
    if (!username.empty() && !password.empty()) {
      const std::string credentials = username + ":" + password;
      const String encoded = base64::encode(credentials.c_str());
      http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
    }

    LOG_DBG("HTTP", "wolfSSL GET: %s", url.c_str());
    const int status = http.GET(
        [&http, &sink](const uint8_t* data, size_t len) {
          if (http.getStatus() != 200) return true;  // ignore body of a redirect/error response
          if (sink.total == 0 && http.hasContentLength()) sink.total = http.getContentLength();
          if (!sink.write(data, len)) return false;
          sink.downloaded += len;
          if (sink.progress && sink.total > 0) sink.progress(sink.downloaded, sink.total);
          return true;
        },
        [&sink]() { return sink.cancelFlag && *sink.cancelFlag; });

    s_lastHttpStatus = status;
    if (http.aborted()) return HttpDownloader::ABORTED;
    if (status < 0) {
      LOG_ERR("HTTP", "wolfSSL request failed: %s", url.c_str());
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
    if (status != 200) {
      LOG_ERR("HTTP", "wolfSSL unexpected status: %d", status);
      return HttpDownloader::HTTP_ERROR;
    }
    sink.contentDisposition = http.getHeader("Content-Disposition");
    if (http.callbackAborted()) return HttpDownloader::FILE_ERROR;
    if (!http.responseComplete()) {
      LOG_ERR("HTTP", "wolfSSL incomplete: got %zu of %zu bytes", sink.downloaded, sink.total);
      return HttpDownloader::HTTP_ERROR;
    }
    return HttpDownloader::OK;
  }
  LOG_ERR("HTTP", "too many redirects");
  return HttpDownloader::HTTP_ERROR;
}
#endif  // FREEINK_NET_WOLFSSL

// Dispatch to the compiled TLS backend. wolfSSL (SecureNet) handles both http and
// https over its own WiFiClient transport, so it is safe for non-TLS targets too.
HttpDownloader::DownloadError runGetSecure(const std::string& url, const std::string& username,
                                           const std::string& password, Sink& sink) {
#if defined(FREEINK_NET_WOLFSSL)
  return runGetWolf(url, username, password, sink);
#else
  return runGet(url, username, password, sink);
#endif
}
}  // namespace

int HttpDownloader::lastHttpStatus() { return s_lastHttpStatus; }

bool HttpDownloader::fetchUrl(const std::string& url, Stream& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) { return outContent.write(data, len) == len; };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, std::string& outContent, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  outContent.clear();  // start clean; the sink appends, so don't carry prior content
  Sink sink;
  sink.write = [&outContent](const uint8_t* data, size_t len) {
    outContent.append(reinterpret_cast<const char*>(data), len);
    return true;
  };
  return runGetSecure(url, username, password, sink) == OK;
}

bool HttpDownloader::fetchUrl(const std::string& url, const DataCallback& onData, const std::string& username,
                              const std::string& password) {
  LOG_DBG("HTTP", "Fetching: %s", url.c_str());
  Sink sink;
  sink.write = onData;
  return runGetSecure(url, username, password, sink) == OK;
}

HttpDownloader::DownloadError HttpDownloader::downloadToFile(const std::string& url, const std::string& destPath,
                                                             ProgressCallback progress, bool* cancelFlag,
                                                             const std::string& username, const std::string& password,
                                                             std::string* serverFilename) {
  LOG_DBG("HTTP", "Downloading: %s -> %s", url.c_str(), destPath.c_str());

  if (Storage.exists(destPath.c_str())) {
    Storage.remove(destPath.c_str());
  }
  HalFile file;
  if (!Storage.openFileForWrite("HTTP", destPath.c_str(), file)) {
    LOG_ERR("HTTP", "Failed to open file for writing");
    return FILE_ERROR;
  }

  Sink sink;
  sink.progress = std::move(progress);
  sink.cancelFlag = cancelFlag;
  sink.write = [&file](const uint8_t* data, size_t len) { return file.write(data, len) == len; };

  const DownloadError result = runGetSecure(url, username, password, sink);
  // Close before any remove() on the same path; DESTRUCTOR_CLOSES_FILE would
  // otherwise close only after the remove.
  file.close();

  if (result != OK) {
    Storage.remove(destPath.c_str());
    return result;
  }
  if (sink.downloaded == 0) {
    LOG_ERR("HTTP", "no data received");
    Storage.remove(destPath.c_str());
    return HTTP_ERROR;
  }
  LOG_DBG("HTTP", "Downloaded %zu bytes", sink.downloaded);

  if (serverFilename) {
    *serverFilename = parseContentDisposition(sink.contentDisposition);
    LOG_DBG("HTTP", "Server filename from Content-Disposition: %s", serverFilename->c_str());
  }
  return OK;
}
