#pragma once

#include <HalStorage.h>
#include <WebServer.h>

#include <string>

class WebDAVHandler : public RequestHandler {
 public:
  // RequestHandler interface
  bool canHandle(WebServer& server, HTTPMethod method, const String& uri) override;
  bool canRaw(WebServer& server, const String& uri) override;
  void raw(WebServer& server, const String& uri, HTTPRaw& raw) override;
  bool handle(WebServer& server, HTTPMethod method, const String& uri) override;

  // Same 4-digit code shown on the device screen; empty = auth disabled. When
  // set, every WebDAV request must carry HTTP Basic credentials whose password
  // equals this code (any username is accepted).
  void setAuthCode(const std::string& code) { _authCode = code; }

 private:
  // PUT streaming state (raw() is called in chunks)
  HalFile _putFile;
  String _putPath;
  bool _putOk = false;
  bool _putExisted = false;

  // Access control: HTTP Basic, password == _authCode (any username). Empty
  // means auth is disabled (server not configured with a code).
  std::string _authCode;
  bool davAuthOk(WebServer& s) const;

  // WebDAV method handlers
  void handleOptions(WebServer& s);
  void handlePropfind(WebServer& s);
  void handleGet(WebServer& s);
  void handleHead(WebServer& s);
  void handlePut(WebServer& s);
  void handleDelete(WebServer& s);
  void handleMkcol(WebServer& s);
  void handleMove(WebServer& s);
  void handleCopy(WebServer& s);
  void handleLock(WebServer& s);
  void handleUnlock(WebServer& s);

  // Utilities
  String getRequestPath(WebServer& s) const;
  String getDestinationPath(WebServer& s) const;
  void urlEncodePath(const String& path, String& out) const;
  bool isProtectedPath(const String& path) const;
  int getDepth(WebServer& s) const;
  bool getOverwrite(WebServer& s) const;
  void sendPropEntry(WebServer& s, const String& href, bool isDir, size_t size, const String& lastModified) const;
  String getMimeType(const String& path) const;
};
