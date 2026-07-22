#include "QRShareActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <esp_task_wdt.h>
#include <qrcode.h>

#include "CrossPointState.h"
#include "WifiSession.h"
#include "activities/ActivityManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

void drawQRCode(const GfxRenderer& renderer, const int x, const int y, const std::string& data) {
  QRCode qrcode;
  uint8_t qrcodeBytes[qrcode_getBufferSize(4)];
  qrcode_initText(&qrcode, qrcodeBytes, 4, ECC_LOW, data.c_str());
  const uint8_t px = 6;
  for (uint8_t cy = 0; cy < qrcode.size; cy++) {
    for (uint8_t cx = 0; cx < qrcode.size; cx++) {
      if (qrcode_getModule(&qrcode, cx, cy)) {
        renderer.fillRect(x + px * cx, y + px * cy, px, px, true);
      }
    }
  }
}

std::string extractFileName(const std::string& path) {
  const auto pos = path.rfind('/');
  return (pos != std::string::npos && pos + 1 < path.size()) ? path.substr(pos + 1) : path;
}

bool isEpubFile(const std::string& name) {
  if (name.size() < 5) return false;
  std::string ext = name.substr(name.size() - 5);
  for (auto& c : ext) c = tolower(c);
  return ext == ".epub";
}

}  // namespace

QRShareActivity::QRShareActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("QRShare", renderer, mappedInput),
      filePath(std::move(filePath)),
      fileName(extractFileName(this->filePath)) {}

void QRShareActivity::onEnter() {
  Activity::onEnter();

  const bool alreadyConnected = (WiFi.status() == WL_CONNECTED) || (WiFi.getMode() == WIFI_AP);
  if (alreadyConnected) {
    startServerAndShowQR();
    return;
  }

  // Connect first via the standard WiFi picker, then start the server on return.
  WiFi.mode(WIFI_STA);
  state = State::WIFI_SELECTION;
  startActivityForResult(makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void QRShareActivity::onWifiSelectionComplete(const bool connected) {
  if (connected) {
    startServerAndShowQR();
    return;
  }
  // User cancelled the picker: no connection was made and no server ran, so a
  // full reboot to reclaim WiFi heap is overkill — power the radio down (DX34
  // did the same) and return straight to the book. onExit then sees
  // WIFI_MODE_NULL and skips the silent restart.
  WiFi.disconnect(false);
  delay(30);
  WiFi.mode(WIFI_OFF);
  delay(30);
  if (!APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else {
    onGoHome();
  }
}

void QRShareActivity::startServerAndShowQR() {
  state = State::QR_DISPLAY;
  if (startServer()) {
    const String ip = (WiFi.getMode() == WIFI_AP) ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
    serverUrl = "http://" + std::string(ip.c_str()) + "/";
  }
  requestUpdate();
}

void QRShareActivity::onExit() {
  stopServer();
  Activity::onExit();

  // Reclaim the WiFi/HTTP heap the same way file transfer does: tear the radio
  // down and silent-restart. Skipped when the radio was never brought up.
  if (WiFi.getMode() != WIFI_MODE_NULL) endWifiSession(WifiReboot::Reader);
}

void QRShareActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onGoHome();
    return;
  }
  if (serverRunning && server) {
    for (int i = 0; i < 200; i++) {
      server->handleClient();
      if (i % 32 == 0) esp_task_wdt_reset();
      yield();
    }
  }
}

void QRShareActivity::render(RenderLock&&) {
  if (state == State::WIFI_SELECTION) return;  // subactivity draws itself

  renderer.clearScreen();
  const int screenW = renderer.getScreenWidth();

  if (!serverRunning) {
    renderer.drawCenteredText(UI_12_FONT_ID, 200, tr(STR_SHARE_SERVER_FAILED), true, EpdFontFamily::REGULAR);
  } else {
    constexpr int LINE_SPACING = 28;
    int y = 30;
    renderer.drawCenteredText(UI_12_FONT_ID, y, tr(STR_SHARE_BOOK), true, EpdFontFamily::REGULAR);
    y += LINE_SPACING + 4;

    const std::string displayName = renderer.truncatedText(UI_10_FONT_ID, fileName.c_str(), screenW - 48);
    renderer.drawCenteredText(UI_10_FONT_ID, y, displayName.c_str());
    y += LINE_SPACING + 8;

    constexpr int qrSize = 33 * 6;  // version 4, 6px/module
    drawQRCode(renderer, (screenW - qrSize) / 2, y, serverUrl);
    y += qrSize + 16;

    renderer.drawCenteredText(SMALL_FONT_ID, y, tr(STR_SCAN_WITH_PHONE));
    y += LINE_SPACING;
    renderer.drawCenteredText(UI_10_FONT_ID, y, serverUrl.c_str(), true, EpdFontFamily::REGULAR);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  // HALF (ghost-cleanup) instead of the FAST default: this frame follows the
  // WiFi picker screens and the QR modules need crisp black/white for scanning.
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool QRShareActivity::startServer() {
  server = makeUniqueNoThrow<WebServer>(80);
  if (!server) {
    LOG_ERR("QRSHARE", "OOM: WebServer");
    return false;
  }
  server->on("/", HTTP_GET, [this] { handleDownload(); });
  server->begin();
  serverRunning = true;
  LOG_INF("QRSHARE", "Server started for: %s", filePath.c_str());
  return true;
}

void QRShareActivity::stopServer() {
  if (server) {
    serverRunning = false;
    server->stop();
    server.reset();
  }
}

void QRShareActivity::handleDownload() {
  if (!Storage.exists(filePath.c_str())) {
    server->send(404, "text/plain", "File not found");
    return;
  }
  auto file = Storage.open(filePath.c_str());
  if (!file || file.isDirectory()) {
    if (file) file.close();
    server->send(500, "text/plain", "Failed to open file");
    return;
  }

  const String contentType = isEpubFile(fileName) ? "application/epub+zip" : "application/octet-stream";
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + String(fileName.c_str()) + "\"");
  server->send(200, contentType.c_str(), "");

  // 4 KB heap buffer (a stack local would eat half this task's stack under the
  // WiFi/HTTP call depth).
  constexpr size_t kCopyBufSize = 4096;
  auto buf = makeUniqueNoThrow<char[]>(kCopyBufSize);
  if (!buf) {
    LOG_ERR("QRSHARE", "OOM download buffer — aborting");
    file.close();
    server->sendContent("");
    return;
  }
  while (file.available()) {
    esp_task_wdt_reset();
    const int bytesRead = file.read(reinterpret_cast<uint8_t*>(buf.get()), kCopyBufSize);
    if (bytesRead <= 0) break;
    server->sendContent(buf.get(), bytesRead);
    yield();
  }
  file.close();
  server->sendContent("");
  LOG_INF("QRSHARE", "Download complete: %s", fileName.c_str());
}
