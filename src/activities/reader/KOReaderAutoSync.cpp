#include "KOReaderAutoSync.h"

#include <Logging.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cstring>
#include <ctime>

#include "KOReaderCredentialStore.h"
#include "KOReaderDocumentId.h"
#include "KOReaderSyncClient.h"
#include "WifiCredentialStore.h"

namespace {

// Survives the silent restart between the network stage and the reading stage.
// RTC_NOINIT memory is deliberately not cleared on a software reset; the magic
// stamp inside the struct is what tells a real handoff from leftover bytes.
RTC_NOINIT_ATTR ko_auto_sync::PendingPull rtcPendingPull;

// Which book was last pulled during this wake. Cleared when the device locks.
RTC_NOINIT_ATTR ko_auto_sync::PullMemory rtcPullMemory;

// Saved networks are read lazily by WifiSelectionActivity, which auto sync never opens,
// so on a boot that goes straight into a book nothing has read them yet. Loaded once per
// boot here: the gate is consulted a handful of times per session, not per page.
void ensureWifiStoreLoaded() {
  static bool loaded = false;
  if (loaded) return;
  WIFI_STORE.loadFromFile();
  loaded = true;
}

std::string documentHashFor(const std::string& path) {
  return KOREADER_STORE.getMatchMethod() == DocumentMatchMethod::FILENAME
             ? KOReaderDocumentId::calculateFromFilename(path)
             : KOReaderDocumentId::calculate(path);
}

// The sync server is HTTPS and certificates are checked against the clock, so a
// device that booted with an unset RTC must learn the time before the handshake.
//
// A device whose clock is already set skips this entirely, which is the single biggest
// saving on the path: the wait below can reach 5 seconds, and it would otherwise be paid
// on every automatic sync, in front of a user waiting for a page.
void syncTimeWithNTP() {
  if (ko_auto_sync::clockLooksSet(static_cast<int64_t>(time(nullptr)))) {
    return;
  }

  if (esp_sntp_enabled()) {
    esp_sntp_stop();
  }
  esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  constexpr int maxRetries = 50;  // 5 seconds
  int retry = 0;
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && retry < maxRetries) {
    vTaskDelay(100 / portTICK_PERIOD_MS);
    retry++;
  }
  if (retry >= maxRetries) {
    LOG_DBG("KOAuto", "NTP sync timeout, using fallback");
  }
}

bool joinNetwork(const WifiCredential& credential, const uint32_t timeoutMs) {
  LOG_DBG("KOAuto", "Joining %s", credential.ssid.c_str());
  if (credential.password.empty()) {
    WiFi.begin(credential.ssid.c_str());
  } else {
    WiFi.begin(credential.ssid.c_str(), credential.password.c_str());
  }

  const uint32_t deadline = millis() + timeoutMs;
  while (millis() < deadline) {
    if (WiFi.status() == WL_CONNECTED) return true;
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
  WiFi.disconnect(true);
  return false;
}

}  // namespace

namespace KOReaderAutoSync {

ko_auto_sync::Gate currentGate() {
  ko_auto_sync::Gate gate;
  gate.autoSyncEnabled = KOREADER_STORE.getAutoSync();
  gate.hasSyncCredentials = KOREADER_STORE.hasCredentials();
  // Only worth the card read once the other two are true; a device with sync switched
  // off never touches wifi.json on its account.
  if (gate.autoSyncEnabled && gate.hasSyncCredentials) {
    ensureWifiStoreLoaded();
    gate.hasWifiCredentials = WIFI_STORE.getCredentialCount() > 0;
  }
  return gate;
}

bool connectSavedWifi(const uint32_t timeoutMs) {
  if (WiFi.status() == WL_CONNECTED) return true;

  const size_t savedCount = WIFI_STORE.getCredentialCount();
  if (savedCount == 0) return false;

  WiFi.persistent(false);  // Credentials belong to WifiCredentialStore, not the SDK's NVS copy
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  // Fast scan, unlike the network picker: it stops at the first matching access point
  // instead of sweeping every channel to find the strongest one. A user standing in front
  // of a book they want open would rather join a weaker AP now than wait for the sweep.
  WiFi.setScanMethod(WIFI_FAST_SCAN);

  // The network the user last joined by hand is the one most likely in range, and gets
  // the whole budget: this is a device that lives in one or two places.
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  if (!lastSsid.empty()) {
    if (const auto credential = WIFI_STORE.findCredential(lastSsid)) {
      if (joinNetwork(*credential, timeoutMs)) return true;
    }
  }

  // Everything else shares one further budget between them, so a card holding eight saved
  // networks and standing in range of none of them still gives up in seconds, not minutes.
  size_t remaining = 0;
  for (size_t i = 0; i < savedCount; i++) {
    const auto ssid = WIFI_STORE.getSsidAt(i);
    if (ssid && *ssid != lastSsid) remaining++;
  }
  if (remaining > 0) {
    const uint32_t perNetworkMs = std::max<uint32_t>(1500, timeoutMs / remaining);
    for (size_t i = 0; i < savedCount; i++) {
      const auto credential = WIFI_STORE.getCredentialAt(i);
      if (!credential) continue;
      if (credential->ssid == lastSsid) continue;  // already tried above
      if (joinNetwork(*credential, perNetworkMs)) return true;
    }
  }

  LOG_DBG("KOAuto", "No saved network reachable");
  return false;
}

void stopWifi() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  esp_wifi_stop();
}

bool pushProgress(const Snapshot& snapshot) {
  const std::string hash = documentHashFor(snapshot.epubPath);
  if (hash.empty()) {
    LOG_ERR("KOAuto", "Could not compute a document hash; skipping push");
    return false;
  }

  syncTimeWithNTP();

  KOReaderProgress progress;
  progress.document = hash;
  progress.progress = snapshot.position.xpath;
  progress.percentage = snapshot.position.percentage;

  // The rich position is a CrossPoint extension; the HTTP client also refuses to
  // serialize it for third-party servers, this check just avoids building it.
  if (KOREADER_STORE.usesCrossPointSyncServer()) {
    KOReaderRichPosition rich;
    const float pct = snapshot.position.percentage < 0.0f   ? 0.0f
                      : snapshot.position.percentage > 1.0f ? 1.0f
                                                            : snapshot.position.percentage;
    rich.pctQ = static_cast<uint32_t>(pct * 1000000.0f + 0.5f);
    rich.spineIndex = static_cast<uint16_t>(snapshot.spineIndex);
    rich.pageNumber = static_cast<uint16_t>(snapshot.pageNumber);
    rich.totalPages = static_cast<uint16_t>(snapshot.totalPagesInSpine > 0 ? snapshot.totalPagesInSpine : 1);
    rich.paragraphIndex = snapshot.paragraphIndex;
    rich.xpath = snapshot.position.xpath;
    progress.position = std::move(rich);
  }

  if (KOREADER_STORE.getSendMetadata()) {
    KOReaderMetadata meta;
    const auto lastSlash = snapshot.epubPath.rfind('/');
    meta.filename = (lastSlash != std::string::npos) ? snapshot.epubPath.substr(lastSlash + 1) : snapshot.epubPath;
    meta.title = snapshot.title;
    meta.authors = snapshot.authors;
    progress.metadata = std::move(meta);
  }

  const auto result = KOReaderSyncClient::updateProgress(progress);
  if (result != KOReaderSyncClient::OK) {
    LOG_ERR("KOAuto", "Push failed: %s (http=%d)", KOReaderSyncClient::errorString(result),
            KOReaderSyncClient::lastHttpCode);
    return false;
  }

  LOG_INF("KOAuto", "Pushed progress %.4f for %s", snapshot.position.percentage, hash.c_str());
  return true;
}

bool fetchAndStashRemote(const std::string& epubPath) {
  clearPendingPull();

  const std::string hash = documentHashFor(epubPath);
  if (hash.empty()) {
    LOG_ERR("KOAuto", "Could not compute a document hash; skipping pull");
    return false;
  }

  syncTimeWithNTP();

  KOReaderProgress remote;
  const auto result = KOReaderSyncClient::getProgress(hash, remote);
  if (result != KOReaderSyncClient::OK) {
    LOG_DBG("KOAuto", "Pull returned %s (http=%d)", KOReaderSyncClient::errorString(result),
            KOReaderSyncClient::lastHttpCode);
    return false;
  }

  if (remote.progress.empty() || remote.progress.size() > ko_auto_sync::kMaxXPathLength) {
    LOG_ERR("KOAuto", "Remote progress carries no usable XPath (%u bytes)", (unsigned)remote.progress.size());
    return false;
  }

  ko_auto_sync::PendingPull pending;
  pending.magic = ko_auto_sync::kPendingPullMagic;
  pending.bookKey = ko_auto_sync::bookKey(epubPath);
  pending.xpathLength = static_cast<uint16_t>(remote.progress.size());
  std::memcpy(pending.xpath, remote.progress.data(), pending.xpathLength);
  pending.xpath[pending.xpathLength] = '\0';
  pending.percentage = remote.percentage;
  if (remote.position.has_value()) {
    pending.spineIndex = remote.position->spineIndex;
    if (remote.position->paragraphIndex.has_value()) {
      pending.paragraphIndex = *remote.position->paragraphIndex;
      pending.hasParagraphIndex = true;
    }
  }

  rtcPendingPull = pending;
  LOG_INF("KOAuto", "Stashed remote progress %.4f for the reader", remote.percentage);
  return true;
}

std::optional<ko_auto_sync::PendingPull> takePendingPull(const std::string& epubPath) {
  if (!ko_auto_sync::pendingPullMatchesBook(rtcPendingPull, ko_auto_sync::bookKey(epubPath))) {
    return std::nullopt;
  }
  const ko_auto_sync::PendingPull pending = rtcPendingPull;
  clearPendingPull();
  return pending;
}

bool hasPendingPullFor(const std::string& epubPath) {
  return ko_auto_sync::pendingPullMatchesBook(rtcPendingPull, ko_auto_sync::bookKey(epubPath));
}

void clearPendingPull() { rtcPendingPull.magic = 0; }

bool pullIsWorthMaking(const std::string& epubPath) {
  return !ko_auto_sync::alreadyPulledThisWake(rtcPullMemory, ko_auto_sync::bookKey(epubPath));
}

void notePullMade(const std::string& epubPath) {
  rtcPullMemory.magic = ko_auto_sync::kPullMemoryMagic;
  rtcPullMemory.bookKey = ko_auto_sync::bookKey(epubPath);
}

void forgetPullsOnLock() { rtcPullMemory.magic = 0; }

}  // namespace KOReaderAutoSync
