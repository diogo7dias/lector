#include "KOReaderAutoSync.h"

#include <Epub.h>
#include <Fnv1a.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_attr.h>

#include <ctime>
#include <vector>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "NearbyPositionReceive.h"
#include "ProgressMapper.h"
#include "WifiCredentialStore.h"
#include "network/opds/BookOrbitMissing.h"
#include "util/AutoSyncJoinPolicy.h"
#include "util/ProgressFile.h"
#include "util/QuoteSync.h"

namespace {

constexpr uint32_t PULL_MAGIC = 0x4B4F5055;  // "KOPU"
constexpr unsigned long JOIN_TIMEOUT_MS = 7000;
constexpr uint32_t FAST_SCAN_MS_PER_CHAN = 100;
constexpr int64_t CLOCK_VALID_FROM = 1577836800;  // 2020-01-01, as HalClock judges it

// RTC_NOINIT survives ESP.restart() and deep sleep, not power loss; the magic tells
// a kept claim from power-on noise.
RTC_NOINIT_ATTR uint32_t pullMagic;
RTC_NOINIT_ATTR uint32_t pulledPathHash;
RTC_NOINIT_ATTR auto_sync_join::BackoffState joinBackoff;

int64_t currentEpoch() {
  const time_t t = time(nullptr);
  return t >= CLOCK_VALID_FROM ? static_cast<int64_t>(t) : 0;
}

// Every book read on another page since the last sleep, oldest first. Each push loads
// the book and talks TLS behind the sleep face, so the list keeps the newest few.
// ponytail: RAM only, so a silent restart between the page turn and the sleep drops
// the push. Every such restart follows a WiFi screen, and a sync is one of them.
constexpr size_t MAX_MOVED = 4;
std::vector<std::string> movedPaths;

// Set when the push on this lock found no saved network in range: the BookOrbit check
// behind it must not pay the same join timeouts again (3 joins of 7 s, radio on).
bool pushJoinFailed = false;

bool enabled() { return KOREADER_STORE.getAutoSync() && KOREADER_STORE.hasCredentials(); }

// The saved position as KOReader reads it. The Epub is released before returning so
// the TLS handshake gets its heap.
bool localProgress(const std::string& path, SavedProgressPosition& out) {
  auto epub = makeUniqueNoThrow<Epub>(path, "/.crosspoint");
  if (!epub) {
    LOG_ERR("KOAUTO", "OOM: Epub");
    return false;
  }
  epub->setupCacheDir();
  if (!epub->load(false, true)) {
    LOG_ERR("KOAUTO", "Cannot load %s", path.c_str());
    return false;
  }
  HalFile f;
  if (!ProgressFile::openForRead("KOAUTO", epub->getCachePath(), f)) return false;
  uint8_t data[10];
  const int n = f.read(data, sizeof(data));
  const auto saved = nearby_position::readCachedPosition(data, n > 0 ? static_cast<size_t>(n) : 0);
  if (!saved) {
    LOG_ERR("KOAUTO", "No usable saved position for %s", path.c_str());
    return false;
  }
  CrossPointPosition pos;
  pos.spineIndex = saved->spine;
  pos.pageNumber = saved->page;
  pos.totalPages = saved->pages;
  if (saved->offset) {
    pos.visibleTextOffset = *saved->offset;
    pos.hasVisibleTextOffset = true;
  }
  out = ProgressMapper::toSavedProgress(*epub, pos);
  return true;
}

bool joinOne(const WifiCredential& cred) {
  LOG_DBG("KOAUTO", "Joining %s", cred.ssid.c_str());
  WiFi.disconnect(false);
  if (cred.password.empty()) {
    WiFi.begin(cred.ssid.c_str());
  } else {
    WiFi.begin(cred.ssid.c_str(), cred.password.c_str());
  }
  const unsigned long start = millis();
  while (millis() - start < JOIN_TIMEOUT_MS) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(100);
  }
  return false;
}

// Fast scan first, then join only saved networks actually seen (plus explicitly
// configured hidden SSIDs). Away from home this spends ~1.3 s scanning once instead
// of 21 s of blind 7 s joins, and backs off across subsequent locks on failure.
bool joinSavedNetwork() {
  const int64_t nowEpoch = currentEpoch();
  if (auto_sync_join::shouldSkip(joinBackoff, nowEpoch)) {
    auto_sync_join::consumeSkippedLock(joinBackoff, nowEpoch);
    LOG_INF("KOAUTO", "Auto-sync skipped (backoff active, failures=%u)",
            static_cast<unsigned>(joinBackoff.consecutiveFailures));
    return false;
  }

  const size_t savedCount = WIFI_STORE.getCredentialCount();
  std::vector<auto_sync_join::SavedNetwork> saved;
  saved.reserve(savedCount);
  for (size_t i = 0; i < savedCount; ++i) {
    if (const auto cred = WIFI_STORE.getCredentialAt(i)) {
      saved.push_back({cred->ssid, cred->hidden});
    }
  }
  if (saved.empty()) return false;

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false);

  const int16_t found =
      WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/false, /*passive=*/false, FAST_SCAN_MS_PER_CHAN);
  std::vector<auto_sync_join::ScannedNetwork> scanned;
  if (found > 0) {
    scanned.reserve(static_cast<size_t>(found));
    for (int i = 0; i < found; ++i) {
      const String ssid = WiFi.SSID(i);
      if (ssid.isEmpty()) continue;
      scanned.push_back({ssid.c_str(), WiFi.RSSI(i)});
    }
  }
  WiFi.scanDelete();

  const std::vector<std::string> candidates =
      auto_sync_join::selectCandidates(saved, scanned, WIFI_STORE.getLastConnectedSsid(), auto_sync_join::MAX_JOINS);
  if (candidates.empty()) {
    LOG_INF("KOAUTO", "No saved network in scan (%d APs); skipping join", static_cast<int>(found));
    auto_sync_join::recordFailure(joinBackoff, nowEpoch);
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  for (const std::string& ssid : candidates) {
    const auto cred = WIFI_STORE.findCredential(ssid);
    if (cred && joinOne(*cred)) {
      auto_sync_join::recordSuccess(joinBackoff);
      return true;
    }
  }
  auto_sync_join::recordFailure(joinBackoff, nowEpoch);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

// No saved network, no radio: checked before any card or radio work.
bool haveSavedNetwork() {
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() > 0) return true;
  LOG_DBG("KOAUTO", "No saved network");
  return false;
}

// True when it left WiFi connected.
bool push(const std::string& path) {
  if (!haveSavedNetwork()) return false;
  SavedProgressPosition local;
  if (!localProgress(path, local)) return false;
  KOReaderProgress progress;
  progress.document = KOReaderDocumentId::calculateFor(path, KOREADER_STORE.getMatchMethod());
  if (progress.document.empty()) {
    LOG_ERR("KOAUTO", "Hash failed for %s", path.c_str());
    return false;
  }
  progress.progress = local.xpath;
  progress.percentage = local.percentage;
  if (WiFi.status() != WL_CONNECTED && !joinSavedNetwork()) {
    LOG_DBG("KOAUTO", "No saved network joined; push skipped");
    pushJoinFailed = true;
    return false;
  }
  const auto result = KOReaderSyncClient::updateProgress(progress);
  if (result == KOReaderSyncClient::OK) quote_sync::push(path, progress.document);
  LOG_INF("KOAUTO", "Pushed %.4f for %s: result=%d http=%d", static_cast<double>(local.percentage), path.c_str(),
          result, KOReaderSyncClient::lastHttpCode);
  return true;
}

}  // namespace

namespace ko_auto_sync {

bool claimPull(const std::string& path) {
  if (!enabled()) return false;
  const uint32_t hash = fnv1a::hash32(path);
  if (pullMagic == PULL_MAGIC && pulledPathHash == hash) return false;
  pullMagic = PULL_MAGIC;
  pulledPathHash = hash;
  return true;
}

void markMoved(const std::string& path) {
  movedPaths.reserve(MAX_MOVED);
  std::erase(movedPaths, path);
  if (movedPaths.size() == MAX_MOVED) movedPaths.erase(movedPaths.begin());
  movedPaths.push_back(path);
}

void onSleep() {
  pullMagic = 0;
  pushJoinFailed = false;
  std::vector<std::string> paths;
  paths.swap(movedPaths);
  if (!enabled()) return;
  const bool pagesMoved = !paths.empty();
  bool wifiUp = false;
  for (const auto& path : paths) {
    if (pushJoinFailed) break;
    wifiUp = push(path) || wifiUp;
  }
  if (!bookorbit_missing::findServer() || !bookorbit_missing::due(wifiUp, pagesMoved)) return;
  if (!wifiUp && (pushJoinFailed || !(haveSavedNetwork() && joinSavedNetwork()))) return;
  bookorbit_missing::refresh();
}

}  // namespace ko_auto_sync
