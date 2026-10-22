#include "KOReaderAutoSync.h"

#include <Epub.h>
#include <Fnv1a.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_attr.h>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "NearbyPositionReceive.h"
#include "ProgressMapper.h"
#include "WifiCredentialStore.h"
#include "activities/reader/ProgressFile.h"

namespace {

constexpr uint32_t PULL_MAGIC = 0x4B4F5055;  // "KOPU"
constexpr unsigned long JOIN_TIMEOUT_MS = 7000;
constexpr size_t MAX_JOINS = 3;

// RTC_NOINIT survives ESP.restart() and deep sleep, not power loss; the magic tells
// a kept claim from power-on noise.
RTC_NOINIT_ATTR uint32_t pullMagic;
RTC_NOINIT_ATTR uint32_t pulledPathHash;

// ponytail: RAM only, so a silent restart between the page turn and the sleep drops
// the push. Every such restart follows a WiFi screen, and a sync is one of them.
std::string movedPath;

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

// Last connected network first, then the others, a few at most: the reader is
// waiting on nothing (the sleep face is up) but the battery is.
bool joinSavedNetwork() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  const std::string last = WIFI_STORE.getLastConnectedSsid();
  size_t joins = 0;
  if (!last.empty()) {
    if (const auto cred = WIFI_STORE.findCredential(last)) {
      if (joinOne(*cred)) return true;
      ++joins;
    }
  }
  for (size_t i = 0; i < WIFI_STORE.getCredentialCount() && joins < MAX_JOINS; ++i) {
    const auto cred = WIFI_STORE.getCredentialAt(i);
    if (!cred || cred->ssid == last) continue;
    if (joinOne(*cred)) return true;
    ++joins;
  }
  return false;
}

void push(const std::string& path) {
  // No saved network, no radio: checked before any card or radio work.
  WIFI_STORE.loadFromFile();
  if (WIFI_STORE.getCredentialCount() == 0) {
    LOG_DBG("KOAUTO", "No saved network; push skipped");
    return;
  }
  SavedProgressPosition local;
  if (!localProgress(path, local)) return;
  KOReaderProgress progress;
  progress.document = KOReaderDocumentId::calculateFor(path, KOREADER_STORE.getMatchMethod());
  if (progress.document.empty()) {
    LOG_ERR("KOAUTO", "Hash failed for %s", path.c_str());
    return;
  }
  progress.progress = local.xpath;
  progress.percentage = local.percentage;
  if (!joinSavedNetwork()) {
    LOG_DBG("KOAUTO", "No saved network joined; push skipped");
    return;
  }
  const auto result = KOReaderSyncClient::updateProgress(progress);
  LOG_INF("KOAUTO", "Pushed %.4f for %s: result=%d http=%d", static_cast<double>(local.percentage), path.c_str(),
          result, KOReaderSyncClient::lastHttpCode);
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

void markMoved(const std::string& path) { movedPath = path; }

void onSleep() {
  pullMagic = 0;
  if (movedPath.empty()) return;
  const std::string path = std::move(movedPath);
  movedPath.clear();
  if (enabled()) push(path);
}

}  // namespace ko_auto_sync
