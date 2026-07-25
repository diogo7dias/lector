#include "util/BookFilingNames.h"

#include <cctype>
#include <functional>

namespace bookfiling {
namespace {

// Lowercased extension including the dot, e.g. ".epub". Empty when there is none.
std::string lowerExtension(const std::string_view path) {
  const size_t dot = path.rfind('.');
  if (dot == std::string_view::npos) return {};
  std::string ext(path.substr(dot));
  for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return ext;
}

}  // namespace

bool isInFolder(const std::string_view path, const std::string_view folder) {
  return path.size() > folder.size() && path.compare(0, folder.size(), folder) == 0 && path[folder.size()] == '/';
}

std::string_view fileNameOf(const std::string_view path) {
  const size_t lastSlash = path.rfind('/');
  return (lastSlash == std::string_view::npos) ? path : path.substr(lastSlash + 1);
}

std::string destinationCandidate(const std::string_view srcPath, const std::string_view folder, const int index) {
  const std::string_view filename = fileNameOf(srcPath);
  std::string dst(folder);
  dst += '/';
  if (index <= 1) {
    dst.append(filename);
    return dst;
  }

  // "name.epub" -> "name (2).epub". A name with no dot takes the suffix at the end.
  const size_t dotPos = filename.rfind('.');
  const std::string_view base = (dotPos == std::string_view::npos) ? filename : filename.substr(0, dotPos);
  const std::string_view ext = (dotPos == std::string_view::npos) ? std::string_view{} : filename.substr(dotPos);
  dst.append(base);
  dst += " (";
  dst.append(std::to_string(index));
  dst += ')';
  dst.append(ext);
  return dst;
}

std::string cacheDirFor(const std::string_view path) {
  const std::string ext = lowerExtension(path);
  const char* prefix = nullptr;
  if (ext == ".epub") {
    prefix = "epub_";
  } else if (ext == ".txt" || ext == ".md") {  // .md is read by the txt reader
    prefix = "txt_";
  } else if (ext == ".xtc") {
    prefix = "xtc_";
  }
  if (prefix == nullptr) return {};
  return std::string("/.crosspoint/") + prefix + std::to_string(std::hash<std::string>{}(std::string(path)));
}

}  // namespace bookfiling
