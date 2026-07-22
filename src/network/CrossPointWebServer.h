#pragma once

#include <FixedBuffer.h>
#include <HalStorage.h>
#include <NetworkUdp.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <memory>
#include <string>

#include "FontUploadPolicy.h"
#include "network/OpdsClient.h"

// Structure to hold file information
struct FileInfo {
  String name;
  size_t size;
  bool isEpub;
  bool isDirectory;
};

class CrossPointWebServer {
 public:
  struct WsUploadStatus {
    bool inProgress = false;
    size_t received = 0;
    size_t total = 0;
    std::string filename;
    std::string lastCompleteName;
    size_t lastCompleteSize = 0;
    unsigned long lastCompleteAt = 0;
  };

  // Used by POST upload handler
  struct UploadState {
    HalFile file;
    String fileName;
    String path = "/";
    size_t size = 0;
    bool success = false;
    String error = "";

    // Upload write buffer - batches small writes into larger SD card operations
    // 4KB is a good balance: large enough to reduce syscall overhead, small enough
    // to keep individual write times short and avoid watchdog issues
    static constexpr size_t UPLOAD_BUFFER_SIZE = 4096;  // 4KB buffer
    FixedBuffer<uint8_t, UPLOAD_BUFFER_SIZE> buffer;
    size_t bufferPos = 0;
  } upload;

  CrossPointWebServer();
  ~CrossPointWebServer();

  // Start the web server (call after WiFi is connected)
  void begin();

  // Stop the web server
  void stop();

  // Call this periodically to handle client requests
  void handleClient();

  // Check if server is running
  bool isRunning() const { return running; }

  WsUploadStatus getWsUploadStatus() const;

  // Get the port number
  uint16_t getPort() const { return port; }

  // The 4-digit per-session access code shown on the device screen; the browser
  // must echo it back (via /api/auth) before any file operation is allowed.
  const char* getAuthCode() const { return authCode; }

  // True when the server runs as an access point (hotspot) with no internet uplink.
  bool isApMode() const { return apMode; }

 private:
  std::unique_ptr<WebServer> server = nullptr;
  std::unique_ptr<WebSocketsServer> wsServer = nullptr;
  bool running = false;
  bool apMode = false;  // true when running in AP mode, false for STA mode
  uint16_t port = 80;
  uint16_t wsPort = 81;  // WebSocket port
  NetworkUDP udp;
  bool udpActive = false;

  // Per-session access control, regenerated on every begin(). authCode is the
  // 4-digit code drawn on the device screen; sessionToken is the opaque value
  // handed to the browser as a cookie once the code is entered. A short lockout
  // throttles brute-force guessing over the LAN.
  char authCode[5] = {0};
  std::string sessionToken;
  uint8_t authFailCount = 0;
  unsigned long authLockoutUntil = 0;

  // WebSocket upload state
  void onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  static void wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length);
  void abortWsUpload(const char* tag);

  // File scanning
  void scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const;
  String formatFileSize(size_t bytes) const;
  bool isEpubFile(const String& filename) const;

  // Access control
  bool isAuthed() const;          // does this request carry a valid session cookie?
  bool requireAuth() const;       // isAuthed() else send 401; gate for every data route
  void handleAuthStatus() const;  // GET  /api/auth  -> whether the code is still needed
  void handleAuthSubmit();        // POST /api/auth  -> validate code, hand out the cookie
  static bool validateWsHeader(String headerName, String headerValue);  // WS handshake gate

  // Request handlers
  void handleRoot() const;
  void handleJszip() const;
  void handleCpAuthJs() const;
  void handleOpdsPage() const;
  void handleNotFound() const;
  void handleStatus() const;
  void handleFileList() const;
  void handleFileListData() const;
  void handleDirCount() const;
  void handleDownload() const;
  void handleUpload(UploadState& state) const;
  void handleUploadPost(UploadState& state) const;
  void handleCreateFolder() const;
  void handleRename() const;
  void handleMove() const;
  void handleMoveBatch() const;
  void handleDelete() const;

  // Settings handlers
  void handleSettingsPage() const;
  void handleGetSettings() const;
  void handlePostSettings();

  // Font management handlers
  void handleFontsPage() const;
  void handleFontList() const;
  void handleFontUpload();
  void handleFontUploadData();
  void handleFontDelete();

  // Font upload state
  struct FontUploadState {
    HalFile file;
    std::string familyName;
    std::string finalPath;
    std::string tempPath;
    std::string backupPath;
    std::string error;
    FontUploadPolicy policy;
    bool valid = false;
    bool committed = false;
    size_t bytesWritten = 0;
    static constexpr size_t BUFFER_SIZE = 4096;
    FixedBuffer<uint8_t, BUFFER_SIZE> buffer;
    size_t bufferPos = 0;
  } fontUpload;

  // OPDS server handlers
  void handleGetOpdsServers() const;
  void handlePostOpdsServer();
  void handleDeleteOpdsServer();

  // Browser-driven OPDS: browse a catalog and download a book on a short-lived
  // background task (network + TLS block for seconds; must not stall the
  // cooperative handleClient loop). Polled via /api/opds/status. STA mode only.
  // One operation at a time — guarded by opdsBusy().
  struct OpdsOp {
    enum class State { Idle, Fetching, Downloading, Done, Error };
    State state = State::Idle;
    bool isDownload = false;
    bool cancel = false;
    size_t downloaded = 0;  // single-writer (task), 32-bit atomic read by poll
    size_t total = 0;
    int httpStatus = 0;
    std::string errorDetail;
    std::string finalName;
    // Captured before the task starts:
    OpdsServer server;
    std::string feedUrl;          // download: feed the book link is relative to
    std::string browseUrl;        // browse: feed URL/path to fetch
    OpdsEntry entry;              // download
    OpdsClient::FeedResult feed;  // browse output, consumed by /api/opds/entries
    TaskHandle_t task = nullptr;
  } opdsOp;

  bool opdsBusy() const {
    return opdsOp.state == OpdsOp::State::Fetching || opdsOp.state == OpdsOp::State::Downloading;
  }
  void handleOpdsBrowse();
  void handleOpdsStatus() const;
  void handleOpdsEntries() const;
  void handleOpdsDownload();
  void handleOpdsCancel();
  void runOpdsTask();
  static void opdsTaskTramp(void* arg);

  // Wi-Fi credential handlers
  void handleGetWifiNetworks() const;
  void handlePostWifiNetwork();
  void handleDeleteWifiNetwork();
};
