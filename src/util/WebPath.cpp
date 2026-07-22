#include "WebPath.h"

#include <FsHelpers.h>

namespace WebPath {
namespace {
// Single source of truth for reserved system directory names. Previously this
// list was duplicated verbatim in CrossPointWebServer.cpp and WebDAVHandler.cpp.
constexpr std::string_view RESERVED_NAMES[] = {"System Volume Information", "XTCache"};
}  // namespace

std::string normalize(std::string_view raw) {
  // FsHelpers::normalisePath collapses duplicate slashes and resolves '..', and
  // emits components joined by single '/' with no leading or trailing slash
  // (empty input yields ""). We then re-assert the leading slash and the
  // root-collapse rule the servers relied on.
  std::string result = FsHelpers::normalisePath(std::string(raw));
  if (result.empty()) {
    return "/";
  }
  if (result.front() != '/') {
    result.insert(result.begin(), '/');
  }
  if (result.size() > 1 && result.back() == '/') {
    result.pop_back();
  }
  return result;
}

bool isReservedName(std::string_view name) {
  for (const std::string_view reserved : RESERVED_NAMES) {
    if (name == reserved) {
      return true;
    }
  }
  return false;
}

bool isProtected(std::string_view path) {
  // Walk each '/'-delimited segment; the operation is forbidden if any segment
  // is hidden (dot-prefixed) or reserved.
  size_t start = 0;
  while (start < path.size()) {
    if (path[start] == '/') {
      ++start;
      continue;
    }
    size_t end = path.find('/', start);
    if (end == std::string_view::npos) {
      end = path.size();
    }
    const std::string_view segment = path.substr(start, end - start);
    if (!segment.empty() && (segment.front() == '.' || isReservedName(segment))) {
      return true;
    }
    start = end;
  }
  return false;
}

}  // namespace WebPath
