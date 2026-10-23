#include "BookOrbitMissing.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "network/TlsScratchHeap.h"
#include "network/opds/OpdsClient.h"
#include "util/BookOrbitMissingFile.h"
#include "util/OpdsFilename.h"
#include "util/UrlUtils.h"

namespace {

constexpr char FILE_PATH[] = "/.crosspoint/bookorbit_missing.txt";
constexpr char SHELF_TITLE[] = "Reading";
constexpr time_t CLOCK_VALID_FROM = 1577836800;  // 2020-01-01, as HalClock judges it
constexpr int FETCH_ATTEMPTS = 1;                // behind the sleep face: one try, no backoff

bookorbit_missing_file::Saved load() {
  char buf[384];
  if (Storage.readFileToBuffer(FILE_PATH, buf, sizeof(buf)) == 0) return {};
  return bookorbit_missing_file::parse(buf);
}

time_t now() {
  const time_t t = time(nullptr);
  return t >= CLOCK_VALID_FROM ? t : 0;
}

bool sameTitle(const std::string& a, const std::string& b) {
  return !a.empty() && strcasecmp(a.c_str(), b.c_str()) == 0;
}

}  // namespace

namespace bookorbit_missing {

const OpdsServer* findServer() {
  for (const auto& server : OPDS_STORE.getServers()) {
    if (server.url.find("/api/v1/opds") != std::string::npos && !server.username.empty()) return &server;
  }
  return nullptr;
}

bool onDevice(const OpdsEntry& book) {
  std::string path;
  path.reserve(96);
  path += SETTINGS.opdsDownloadFolder;
  path += '/';
  path += opdsBookFilename(book.author, book.title, static_cast<OpdsFilenameFormat>(SETTINGS.opdsFilenameFormat));
  if (Storage.exists(path.c_str())) return true;
  for (const RecentBook& recent : RECENT_BOOKS.getBooks()) {
    if (sameTitle(recent.title, book.title) && !RecentBooksStore::isMissing(recent)) return true;
  }
  return false;
}

size_t countBooks(const std::vector<OpdsEntry>& entries) {
  return std::count_if(entries.begin(), entries.end(),
                       [](const OpdsEntry& e) { return e.type == OpdsEntryType::BOOK; });
}

size_t keepMissing(std::vector<OpdsEntry>& entries) {
  entries.erase(std::remove_if(entries.begin(), entries.end(),
                               [](const OpdsEntry& e) { return e.type == OpdsEntryType::BOOK && onDevice(e); }),
                entries.end());
  return countBooks(entries);
}

int savedCount() { return load().count; }

std::string savedFeedUrl() { return load().feedUrl; }

void save(const int count, const std::string& feedUrl) {
  char head[40];
  snprintf(head, sizeof(head), "%d %lld\n", count, static_cast<long long>(now()));
  String content(head);
  content += feedUrl.c_str();
  content += '\n';
  if (!Storage.writeFile(FILE_PATH, content)) LOG_ERR("BOMISS", "Cannot write %s", FILE_PATH);
}

bool due(const bool wifiUp) { return bookorbit_missing_file::due(wifiUp, now(), load().checkedAt); }

void refresh() {
  GfxRenderer& renderer = activityManager.getRenderer();
  const OpdsServer* found = findServer();
  if (!found) return;
  const OpdsServer server = *found;
  // Stamped before the fetch: a server that is down or not yet trusted waits the
  // full 6 hours too, instead of costing a WiFi join on every lock.
  const auto previous = load();
  save(previous.count, previous.feedUrl.empty() ? server.url : previous.feedUrl);
  opds::OpdsClient client;

  // The sleep face is already on the panel, so its buffer can carry the TLS heap.
  std::unique_ptr<GfxRenderer::FrameBufferLoan> loan;
  std::unique_ptr<tls_scratch::Session> tlsScratch;
  const auto before = [&] {
    loan = makeUniqueNoThrow<GfxRenderer::FrameBufferLoan>(renderer);
    tlsScratch = makeUniqueNoThrow<tls_scratch::Session>();
  };
  const auto after = [&] {
    tlsScratch.reset();
    loan.reset();
  };
  const auto fetch = [&](const std::string& url, opds::FeedResult& out) {
    return client.fetchFeed(server, url, out, nullptr, nullptr, opds_retry::DEFAULT_TIMEOUT_MS, FETCH_ATTEMPTS, before,
                            after);
  };

  // The shelf's id differs per server, so it is found by name under smart-scopes.
  std::string feedUrl;
  {
    const std::string scopesUrl = UrlUtils::buildUrl(server.url, "smart-scopes");
    opds::FeedResult scopes;
    const auto status = fetch(scopesUrl, scopes);
    if (status != opds::ClientStatus::OK) {
      LOG_INF("BOMISS", "Smart shelves fetch failed (%d)", static_cast<int>(status));
      return;
    }
    for (const auto& e : scopes.entries) {
      if (e.type == OpdsEntryType::NAVIGATION && sameTitle(e.title, SHELF_TITLE)) {
        feedUrl = UrlUtils::buildUrl(scopesUrl, e.href);
        break;
      }
    }
  }
  if (feedUrl.empty()) {
    LOG_INF("BOMISS", "No \"%s\" smart shelf on %s", SHELF_TITLE, server.url.c_str());
    save(0, server.url);
    return;
  }

  opds::FeedResult reading;
  const auto status = fetch(feedUrl, reading);
  if (status != opds::ClientStatus::OK && status != opds::ClientStatus::EMPTY) {
    LOG_INF("BOMISS", "Reading shelf fetch failed (%d)", static_cast<int>(status));
    return;
  }
  const size_t missing = keepMissing(reading.entries);
  LOG_INF("BOMISS", "%u reading on BookOrbit, not on the card", static_cast<unsigned>(missing));
  save(static_cast<int>(missing), feedUrl);
}

}  // namespace bookorbit_missing
