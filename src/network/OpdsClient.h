#pragma once
#include <OpdsParser.h>

#include <string>
#include <vector>

#include "OpdsServerStore.h"
#include "network/HttpDownloader.h"

// UI-free OPDS operations shared by the on-device browser
// (OpdsBookBrowserActivity) and the web file server's OPDS routes. No rendering,
// no i18n, no activity state. Single-threaded per call, like HttpDownloader
// (lastHttpStatus() is static state): only ever run one OPDS operation at a time.
namespace OpdsClient {

enum class FeedStatus { OK, NoUrl, HeapLow, FetchFailed, ParseFailed, Empty };

struct FeedResult {
  std::vector<OpdsEntry> entries;  // raw parser entries; caller adds any synthetic prev/next
  std::string searchTemplate;
  std::string nextPageUrl;
  std::string prevPageUrl;
};

// Resolve pathOrUrl against server.url (an absolute http(s) URL is used as-is),
// apply the <20KB largest-free-block pre-gate, then fetch + parse the feed into
// `out`. Frees `out`'s previous contents first.
FeedStatus fetchFeed(const OpdsServer& server, const std::string& pathOrUrl, FeedResult& out);

// Build the download URL relative to feedUrl (falls back to server.url), ensure
// SETTINGS.opdsDownloadFolder (mkdir, else SD root), compose the filename via
// opdsBookFilename, download, apply the keepFilename rename, clear the book
// cache. outFinalPath receives the path written on success. progress/cancelFlag
// are forwarded to HttpDownloader and may be null.
HttpDownloader::DownloadError downloadBook(const OpdsServer& server, const std::string& feedUrl, const OpdsEntry& entry,
                                           std::string& outFinalPath, const HttpDownloader::ProgressCallback& progress,
                                           bool* cancelFlag);

}  // namespace OpdsClient
