#pragma once

// Read side of the CLX1 index. Holds one open handle and a header: nothing that
// scales with the library stays resident. See LibraryFormat.h for the layout.

#include <HalStorage.h>

#include <cstdint>
#include <string>

#include "LibraryFormat.h"

namespace library {

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

 private:
  bool readAt(uint32_t offset, void* dst, size_t len);
  bool readText(const ClixRecord& record, size_t skip, size_t len, std::string& out);

  HalFile file;
  ClixHeader head{};
  bool opened = false;
  bool readFailed = false;
  ClixValidity lastValidity = ClixValidity::BadMagic;
};

}  // namespace library
