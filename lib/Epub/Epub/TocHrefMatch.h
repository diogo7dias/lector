#pragma once

#include <cstdint>
#include <string>
#include <string_view>

// Resolves a TOC entry's target document to a spine index.
//
// The exact path is the answer whenever the spine carries it. It often does not: a TOC
// document resolves its hrefs against its own folder, so the path built from it can name
// the right file through a different directory prefix than the manifest used, and an
// unresolvable entry used to leave the reader with a chapter row that did nothing at all.
// The file name then decides -- but only when exactly one spine item carries it, because
// two folders holding an index.html make any guess a wrong chapter.
//
// Streaming on purpose: the spine is read one entry at a time from the SD card during the
// TOC pass, and holding every href in RAM is what this device does not have.
class TocHrefMatch {
  std::string target;
  std::string_view targetFileName;
  int16_t exactIndex = -1;
  int16_t fileNameIndex = -1;
  int fileNameMatches = 0;

  static std::string_view fileNameOf(const std::string_view path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
  }

 public:
  explicit TocHrefMatch(std::string tocHref) : target(std::move(tocHref)), targetFileName(fileNameOf(target)) {}

  void consider(const int16_t spineIndex, const std::string& spineHref) {
    if (target.empty() || exactIndex != -1) {
      return;
    }
    if (spineHref == target) {
      exactIndex = spineIndex;
      return;
    }
    if (!targetFileName.empty() && fileNameOf(spineHref) == targetFileName) {
      fileNameMatches++;
      fileNameIndex = spineIndex;
    }
  }

  // Spine index for the TOC entry, or -1 when the book gives no unambiguous answer.
  int16_t result() const {
    if (exactIndex != -1) {
      return exactIndex;
    }
    return fileNameMatches == 1 ? fileNameIndex : static_cast<int16_t>(-1);
  }
};
