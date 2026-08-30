#include "ClockSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClockSyncActivity::onEnter() {
  UiStatusActivity::onEnter();
  state = SYNCING;
  syncedTime[0] = '\0';

  if (WiFi.status() == WL_CONNECTED) {
    requestUpdate();
    return;
  }

  shouldTearDownWifiOnExit = true;
  launchWifiSelection();
}

void ClockSyncActivity::onExit() {
  Activity::onExit();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    silentRestart();
  }
}

void ClockSyncActivity::launchWifiSelection() {
  LOG_INF("CLK", "Manual sync requested without WiFi, launching WiFi selection");
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ClockSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    LOG_INF("CLK", "WiFi selection cancelled before manual clock sync");
    finish();
    return;
  }

  state = SYNCING;
  requestUpdate();
}

void ClockSyncActivity::runSync() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_INF("CLK", "Manual sync requested but WiFi is not connected after selection");
    state = NO_WIFI;
    requestUpdate();
    return;
  }

  const bool ok = halClock.syncFromNTP();
  if (!ok) {
    state = FAILED;
    requestUpdate();
    return;
  }

  // Mark as synced so the auto-sync hook stops firing on future WiFi connects.
  SETTINGS.clockHasBeenSynced = 1;
  SETTINGS.saveToFile();

  // Read the freshly synced time back for the user-facing confirmation.
  char buf[9];
  if (halClock.formatTime(buf, sizeof(buf), SETTINGS.clockUtcOffsetQ, SETTINGS.clockFormat == 1)) {
    snprintf(syncedTime, sizeof(syncedTime), "%s", buf);
    // Sized for the label in any language: STR_CURRENT_TIME is 26 bytes in
    // Russian (UTF-8 Cyrillic is 2 bytes per letter) versus 13 in English,
    // plus a separator and up to "08:56 PM".
    snprintf(syncedLine, sizeof(syncedLine), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
  }
  state = SUCCESS;
  requestUpdate();
}

bool ClockSyncActivity::handleCustomInput() {
  if (state != SYNCING) return false;
  // First tick paints "Syncing...", then the blocking sync runs: without the
  // wait the screen would still show whatever came before it.
  requestUpdateAndWait();
  runSync();
  return true;
}

UiStatusActivity::StatusView ClockSyncActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_CLOCK_SYNC);
  switch (state) {
    case SYNCING:
      view.lines = {tr(STR_CLOCK_SYNCING), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case SUCCESS:
      view.lines = {tr(STR_CLOCK_SYNC_OK), syncedLine[0] ? syncedLine : nullptr, nullptr, nullptr};
      break;
    case NO_WIFI:
      view.lines = {tr(STR_CLOCK_SYNC_NO_WIFI), tr(STR_CLOCK_SYNC_NO_WIFI_HINT), nullptr, nullptr};
      break;
    case FAILED:
      view.lines = {tr(STR_CLOCK_SYNC_FAIL), tr(STR_CHECK_SERIAL_OUTPUT), nullptr, nullptr};
      break;
  }
  return view;
}
