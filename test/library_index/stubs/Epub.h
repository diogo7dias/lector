#pragma once
// Host-test stub for lib/Epub/Epub.h: loadMetadata answers from a table the test fills,
// and counts how many books the builder asked about.
#include <map>
#include <string>
#include <utility>

namespace fake_epub {
struct SeriesInfo {
  std::string name;
  uint16_t indexX10 = 0;
};
inline std::map<std::string, std::pair<std::string, std::string>> metadata;  // path -> title, author
inline std::map<std::string, SeriesInfo> series;                             // path -> series, indexX10
inline unsigned parses = 0;
}  // namespace fake_epub

class Epub {
 public:
  Epub(std::string path, const std::string&) : path(std::move(path)) {}
  bool loadMetadata(std::string& title, std::string& author) {
    std::string unusedSeries;
    uint16_t unusedIndex = 0;
    return loadMetadata(title, author, unusedSeries, unusedIndex);
  }
  bool loadMetadata(std::string& title, std::string& author, std::string& seriesOut, uint16_t& seriesIndexOut) {
    fake_epub::parses++;
    const auto it = fake_epub::metadata.find(path);
    if (it == fake_epub::metadata.end()) return false;
    title = it->second.first;
    author = it->second.second;
    const auto sit = fake_epub::series.find(path);
    if (sit != fake_epub::series.end()) {
      seriesOut = sit->second.name;
      seriesIndexOut = sit->second.indexX10;
    } else {
      seriesOut.clear();
      seriesIndexOut = 0;
    }
    return true;
  }

 private:
  std::string path;
};
