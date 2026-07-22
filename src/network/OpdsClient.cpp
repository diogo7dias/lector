#include "OpdsClient.h"

#include <HalStorage.h>
#include <Logging.h>
#include <OpdsStream.h>
#include <esp_heap_caps.h>

#include "CrossPointSettings.h"
#include "util/BookCacheUtils.h"
#include "util/OpdsFilename.h"
#include "util/StringUtils.h"
#include "util/UrlUtils.h"

namespace OpdsClient {

FeedStatus fetchFeed(const OpdsServer& server, const std::string& pathOrUrl, FeedResult& out) {
  std::vector<OpdsEntry>().swap(out.entries);
  out.searchTemplate.clear();
  out.nextPageUrl.clear();
  out.prevPageUrl.clear();

  if (server.url.empty()) return FeedStatus::NoUrl;

  // Pre-fetch heap gate: a TLS fetch on a fragmented heap walks every parse
  // allocation past an abort() cliff. Refuse rather than crash — the caller can
  // surface an error and let the user reboot to reclaim the LWIP/wolfSSL heap.
  const size_t largestBlock = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
  if (largestBlock < 20 * 1024) {
    LOG_ERR("OPDSCLI", "Heap too fragmented for a fetch (largest=%u)", static_cast<unsigned>(largestBlock));
    return FeedStatus::HeapLow;
  }

  const std::string url = (pathOrUrl.find("http") == 0) ? pathOrUrl : UrlUtils::buildUrl(server.url, pathOrUrl);
  LOG_DBG("OPDSCLI", "Fetching: %s", url.c_str());

  OpdsParser parser;
  {
    OpdsParserStream stream{parser};
    if (!HttpDownloader::fetchUrl(url, stream, server.username, server.password)) {
      return FeedStatus::FetchFailed;
    }
  }
  if (!parser) {
    // A tiny 200 body that fails to parse is usually an HTML login page.
    LOG_ERR("OPDSCLI", "feed parse failed (status=%d)", HttpDownloader::lastHttpStatus());
    return FeedStatus::ParseFailed;
  }

  out.searchTemplate = parser.getSearchTemplate();
  out.nextPageUrl = parser.getNextPageUrl();
  out.prevPageUrl = parser.getPrevPageUrl();
  out.entries = std::move(parser).getEntries();

  return out.entries.empty() ? FeedStatus::Empty : FeedStatus::OK;
}

HttpDownloader::DownloadError downloadBook(const OpdsServer& server, const std::string& feedUrl, const OpdsEntry& entry,
                                           std::string& outFinalPath, const HttpDownloader::ProgressCallback& progress,
                                           bool* cancelFlag) {
  // Build the full download URL relative to the current feed, not the root URL.
  const std::string& base = feedUrl.empty() ? server.url : feedUrl;
  const std::string downloadUrl = UrlUtils::buildUrl(base, entry.href);

  // opdsDownloadFolder is a null-terminated char[64]. On mkdir failure fall back
  // to SD root so a download is never lost (upstream #2571).
  const char* folder = SETTINGS.opdsDownloadFolder;  // "" => SD root
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

  std::string serverFilename;
  const auto result = HttpDownloader::downloadToFile(downloadUrl, filename, progress, cancelFlag, server.username,
                                                     server.password, server.keepFilename ? &serverFilename : nullptr);

  if (result == HttpDownloader::OK) {
    // When the server sends a filename (Content-Disposition) and this server is
    // set to keep it, rename the download to that name in the same folder.
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
  }
  return result;
}

}  // namespace OpdsClient
