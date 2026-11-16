#pragma once
// Every entry "exists" with a size derived from its path, except paths containing "missing",
// so buildBookBin computes real cumulative sizes for whatever the OPF names.
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

class ZipFile {
 public:
  struct SizeTarget {
    uint64_t hash;
    uint16_t len;
    uint16_t index;
  };

  static uint64_t fnvHash64(const char*, size_t) { return 0; }
  explicit ZipFile(const std::string&) {}
  bool open() { return true; }
  bool close() { return true; }
  int fillUncompressedSizes(std::deque<SizeTarget>&, std::deque<uint32_t>&) { return 0; }
  bool getInflatedFileSize(const char* path, size_t* size) const {
    const std::string p(path);
    if (p.find("missing") != std::string::npos) return false;
    *size = 1 + (p.size() * 2654435761u) % 400000;
    return true;
  }
};
