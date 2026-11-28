#pragma once

// Read side of the CLX1 index. Holds one open handle and a header: nothing that
// scales with the library stays resident. See LibraryFormat.h for the layout.

#include <HalStorage.h>

#include <cstdint>
#include <string>
#include <vector>

#include "LibraryFormat.h"

namespace library {

using ViewProgressFn = void (*)(void* ctx);

class LibraryIndexFile {
 public:
  LibraryIndexFile() = default;
  LibraryIndexFile(const LibraryIndexFile&) = delete;
  LibraryIndexFile& operator=(const LibraryIndexFile&) = delete;

  // Open and validate. On failure validity() says why.
  bool open(const char* path);
  void close();
  bool isOpen() const { return opened; }
  bool ioFailed() const { return readFailed; }
  ClixValidity validity() const { return lastValidity; }
  const ClixHeader& header() const { return head; }
  uint16_t bookCount() const { return opened ? head.bookCount : 0; }
  bool metadataEnabled() const { return opened && (head.flags & CLIX_FLAG_NO_METADATA) == 0; }
  bool hasSeriesMetadata() const { return opened && (head.flags & CLIX_FLAG_HAS_SERIES) != 0; }

  // Up to `count` records from `first` in one read. Returns how many were read,
  // each already checked with recordInBounds (a bad one stops the batch).
  size_t readRecords(uint16_t first, size_t count, ClixRecord* out);
  bool readRecord(uint16_t ordinal, ClixRecord& out) { return readRecords(ordinal, 1, &out) == 1; }

  // The text of one record. Each is cleaned again on the way out: the file is on
  // a card anyone can edit, so reading trusts the bounds, not the bytes.
  bool readTitle(const ClixRecord& record, std::string& out);
  bool readAuthor(const ClixRecord& record, std::string& out);
  // Absolute path of the book.
  bool readPath(const ClixRecord& record, std::string& out);
  bool readSeries(const ClixRecord& record, std::string& out);

 private:
  bool readAt(uint32_t offset, void* dst, size_t len);
  bool readText(const ClixRecord& record, size_t skip, size_t len, std::string& out);

  HalFile file;
  ClixHeader head{};
  bool opened = false;
  bool readFailed = false;
  ClixValidity lastValidity = ClixValidity::BadMagic;
};

// Update percent and readOrder in place for `bookPath` inside an existing CLX v2 index.
// Skips the write when unchanged. Returns false if the index is missing/invalid or the
// book is not present (e.g. moved since the last build).
bool updateRecordProgress(const char* indexPath, const char* bookPath, uint8_t percent, uint32_t readOrder);

// Build an ordered list of record ordinals matching `filter` and sorted by `sort`.
// Leaves `out` empty and returns true for the default (Title + All), which needs no
// remapping table. Returns false on OOM or read failure.
bool buildLibraryView(LibraryIndexFile& index, LibrarySort sort, LibraryFilter filter, std::vector<uint16_t>& out,
                      ViewProgressFn progress = nullptr, void* progressCtx = nullptr);

}  // namespace library
