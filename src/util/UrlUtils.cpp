#include "UrlUtils.h"

namespace UrlUtils {

std::string ensureProtocol(const std::string& url) {
  if (url.find("://") == std::string::npos) {
    return "http://" + url;
  }
  return url;
}

std::string extractHost(const std::string& url) {
  const size_t protocolEnd = url.find("://");
  if (protocolEnd == std::string::npos) {
    // No protocol, find first slash
    const size_t firstSlash = url.find('/');
    return firstSlash == std::string::npos ? url : url.substr(0, firstSlash);
  }
  // Find the first slash after the protocol
  const size_t hostStart = protocolEnd + 3;
  const size_t pathStart = url.find('/', hostStart);
  return pathStart == std::string::npos ? url : url.substr(0, pathStart);
}

std::string buildUrl(const std::string& serverUrl, const std::string& path) {
  // If path is already an absolute URL (has protocol), use it directly
  if (path.find("://") != std::string::npos) {
    return path;
  }
  const std::string urlWithProtocol = ensureProtocol(serverUrl);
  if (path.empty()) {
    return urlWithProtocol;
  }
  if (path[0] == '/') {
    // Absolute path - use just the host
    return extractHost(urlWithProtocol) + path;
  }
  // Relative path - strip query string from base before resolving
  std::string base = urlWithProtocol;
  const size_t queryPos = base.find('?');
  if (queryPos != std::string::npos) {
    base.resize(queryPos);
  }
  // RFC 3986 5.3 merge: a relative reference replaces the base URL's last
  // path segment (a feed at /opds/new linking "page2" means /opds/page2).
  // Only look for '/' after the host so the merge never cuts into it.
  const size_t protocolEnd = base.find("://");
  const size_t hostStart = protocolEnd == std::string::npos ? 0 : protocolEnd + 3;
  const size_t pathStart = base.find('/', hostStart);
  if (pathStart == std::string::npos) {
    return base + "/" + path;
  }
  return base.substr(0, base.rfind('/') + 1) + path;
}

}  // namespace UrlUtils
