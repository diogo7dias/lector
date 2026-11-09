#pragma once

#include <FsHelpers.h>
#include <string.h>
#include <strings.h>

#include <string>
#include <string_view>

// What the web file manager and WebDAV must refuse. Pure, so the rules run on the host.
namespace protected_paths {

inline constexpr const char* STORE_FOLDER = "/.crosspoint";

// Stored passwords, plus the certificate pins: overwriting those would make the device
// trust another server. Prefix match, so temp and backup siblings are covered too.
inline constexpr const char* PROTECTED_FILES[] = {"/.crosspoint/wifi.json", "/.crosspoint/opds.json",
                                                  "/.crosspoint/koreader.json", "/.crosspoint/trusted_certs.txt"};

// SdFat also opens a name by its generated 8.3 alias (".crosspoint" is CROSSP~1), and an
// alias cannot be mapped back to its long name.
inline bool isShortAlias(const std::string_view name) {
  const size_t tilde = name.find('~');
  if (tilde == std::string_view::npos || tilde + 1 >= name.size() || name[tilde + 1] < '0' || name[tilde + 1] > '9') {
    return false;
  }
  const size_t dot = name.find('.');
  const size_t base = dot == std::string_view::npos ? name.size() : dot;
  const size_t ext = dot == std::string_view::npos ? 0 : name.size() - dot - 1;
  return base <= 8 && ext <= 3;
}

// SdFat skips leading spaces and drops trailing dots and spaces when it opens a name.
inline std::string_view nameSdFatOpens(std::string_view name) {
  while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
  while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.remove_suffix(1);
  return name;
}

// One name exactly as a directory listing returns it, so aliases cannot occur here.
inline bool isProtectedName(std::string_view name) {
  while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
  if (!name.empty() && name.front() == '.') return true;
  return FsHelpers::isSystemFolderName(nameSdFatOpens(name));
}

inline bool isProtectedFile(const std::string_view path) {
  std::string canon;
  canon.reserve(path.size() + 1);
  int depth = 0;
  size_t start = 0;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    const std::string_view raw = path.substr(start, end - start);
    start = end + 1;
    if (raw == "..") {
      const size_t slash = canon.rfind('/');
      canon.erase(slash == std::string::npos ? 0 : slash);
      if (depth > 0) depth--;
      continue;
    }
    const std::string_view name = nameSdFatOpens(raw);
    if (name.empty()) continue;
    const bool insideStoreFolder = depth == 1 && strcasecmp(canon.c_str(), STORE_FOLDER) == 0;
    if ((depth == 0 || insideStoreFolder) && isShortAlias(name)) return true;
    canon += '/';
    canon.append(name);
    depth++;
  }
  for (const char* file : PROTECTED_FILES) {
    if (strncasecmp(canon.c_str(), file, strlen(file)) == 0) return true;
  }
  return false;
}

// A request path: refused when any segment is protected, not just the last one.
inline bool isProtectedPath(const std::string_view path) {
  if (isProtectedFile(path)) return true;
  size_t start = 0;
  while (start < path.size()) {
    size_t end = path.find('/', start);
    if (end == std::string_view::npos) end = path.size();
    if (end > start && isProtectedName(path.substr(start, end - start))) return true;
    start = end + 1;
  }
  return false;
}

}  // namespace protected_paths
