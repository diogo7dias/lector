#pragma once

#include <HalStorage.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// On-SD file listing for very large folders.
//
// Filenames live in a single temp file on the SD card; RAM holds only a 4-byte
// offset per entry (in natural, directories-first sorted order) plus a couple of
// transient name buffers. The directory is streamed once and the offsets are
// ordered with a chunked external merge sort, so peak RAM stays bounded even for
// thousands of entries — where the plain std::vector<std::string> listing used by
// FileBrowserActivity would fragment the heap or OOM.
//
// Only engaged above FileBrowserActivity's threshold; small folders keep the
// in-RAM path untouched.
class SdFileIndex {
 public:
  // Decides whether a raw directory entry is listed. `name` is NUL-terminated.
  using AcceptFn = std::function<bool(const char* name, bool isDir)>;

  SdFileIndex() = default;
  ~SdFileIndex() { clear(); }
  SdFileIndex(const SdFileIndex&) = delete;
  SdFileIndex& operator=(const SdFileIndex&) = delete;

  // Stream `dirPath`, write accepted names to the SD temp file, and build the
  // sorted offset table. Returns false (and leaves valid() false) on any failure.
  bool build(const std::string& dirPath, const AcceptFn& accept);

  // Release the offset table and close/remove the temp file.
  void clear();

  bool valid() const { return valid_; }
  size_t count() const { return offsets_.size(); }
  // Number of leading directory rows (directories sort first); the file tail begins here.
  size_t firstFileIndex() const { return firstFileIdx_; }

  // Display name (with trailing '/' for directories) for row i, or "" on error.
  std::string nameAt(size_t i) const;

  // Fisher-Yates shuffle over the non-directory tail (rows >= firstFileIndex()).
  void shuffleTail();

  // First index whose display name is not ordered before `name` (natural order),
  // i.e. the std::lower_bound insertion slot. Range [0, count()].
  size_t lowerBound(const std::string& name) const;

 private:
  std::string namesPath_;          // temp names file on the SD card
  std::vector<uint32_t> offsets_;  // sorted display order -> record offset in namesPath_
  size_t firstFileIdx_ = 0;
  bool valid_ = false;
  mutable HalFile readFile_;  // kept open for name lookups after build

  std::string readNameAtOffset(uint32_t offset) const;
  bool externalSort();
};
