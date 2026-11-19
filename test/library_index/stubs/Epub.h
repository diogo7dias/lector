#pragma once
// Host-test stub for lib/Epub/Epub.h: loadMetadata answers from a table the test fills,
// and counts how many books the builder asked about.
#include <map>
#include <string>
#include <utility>

namespace fake_epub {
inline std::map<std::string, std::pair<std::string, std::string>> metadata;  // path -> title, author
inline unsigned parses = 0;
}  // namespace fake_epub

class Epub {
 public:
  Epub(std::string path, const std::string&) : path(std::move(path)) {}
  bool loadMetadata(std::string& title, std::string& author) {
    fake_epub::parses++;
    const auto it = fake_epub::metadata.find(path);
    if (it == fake_epub::metadata.end()) return false;
    title = it->second.first;
    author = it->second.second;
    return true;
  }

 private:
  std::string path;
};
