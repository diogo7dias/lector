#pragma once

// On-disk layout of the Library index (`CLX1`), plus the pure validation and
// text helpers around it. Free of HalStorage and Arduino so it is host-tested
// (test/library_index).
//
// Layout, in this order, each section starting on a 512-byte boundary:
//
//   header   64 bytes of struct, padded to 512
//   records  N x exactly 32 bytes, in title order (the order the Library draws)
//   names    per record, in the same order: title, author, path
//
// The fixed record stride is the load-bearing choice: row k is at
// recordStart + 32k, so a page of rows is one read of the records and one small
// read per row for its text, with no offset table held in RAM. 16 records fill a
// sector exactly.
//
// Not compatible with CrossPoint's CLX1 (a different record and a different
// version byte), and written to its own file, so a card moved between the two
// firmwares never makes them rebuild each other's index.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace library {

inline constexpr char CLIX_MAGIC[4] = {'C', 'L', 'X', '1'};
// Bumping this is the whole migration: an index of another version fails
// validation and is rebuilt.
inline constexpr uint8_t CLIX_FORMAT_VERSION = 1;

inline constexpr uint32_t CLIX_ALIGN = 512;

// Bounds on what a book may tell us, enforced at build time and again when
// reading. Titles and authors come from files on the card: untrusted.
inline constexpr size_t CLIX_MAX_TITLE = 128;
inline constexpr size_t CLIX_MAX_AUTHOR = 96;
inline constexpr size_t CLIX_MAX_PATH = 255;

// Books listed. Past it the walk stops and the header says so. The C3 holds the
// sort keys of the build in RAM (see LibraryBuilder.h), so it lists fewer.
#if defined(BOARD_HAS_PSRAM) || !defined(ARDUINO)
inline constexpr uint16_t CLIX_MAX_RECORDS = 4096;
#else
inline constexpr uint16_t CLIX_MAX_RECORDS = 2048;
#endif

enum ClixFlags : uint8_t {
  CLIX_FLAG_TRUNCATED = 1 << 0,  // more books on the card than CLIX_MAX_RECORDS
};

#pragma pack(push, 1)

struct ClixHeader {
  char magic[4];
  uint8_t formatVersion;
  uint8_t flags;
  uint16_t bookCount;
  uint32_t recordStart;
  uint32_t nameStart;
  uint32_t nameLen;
  // Expected total file size. Comparing it with the real size is a free
  // truncation guard: a file cut short cannot pass.
  uint32_t selfSize;
  uint8_t reserved[40];
};
static_assert(sizeof(ClixHeader) == 64, "ClixHeader must be exactly 64 bytes");

struct ClixRecord {
  uint64_t pathHash;  // FNV-1a 64 of the path: how a rebuild finds the book again
  uint32_t nameOff;   // from nameStart: title, author, path, back to back
  uint32_t fileSize;  // with modified, the identity a rebuild trusts to keep the text
  uint32_t modified;  // FAT date << 16 | FAT time; 0 when the card gave none
  uint8_t titleLen;
  uint8_t authorLen;
  uint8_t pathLen;
  uint8_t reserved[9];
};
static_assert(sizeof(ClixRecord) == 32, "ClixRecord must be exactly 32 bytes");
static_assert(CLIX_ALIGN % sizeof(ClixRecord) == 0, "records must tile a 512-byte sector");

#pragma pack(pop)

inline constexpr size_t CLIX_ROW_NAME_BYTES = CLIX_MAX_TITLE + CLIX_MAX_AUTHOR + CLIX_MAX_PATH;

inline uint64_t clixPathHash(const char* data, const size_t len) {
  uint64_t hash = 14695981039346656037ULL;  // FNV-1a 64
  for (size_t i = 0; i < len; i++) {
    hash ^= static_cast<unsigned char>(data[i]);
    hash *= 1099511628211ULL;
  }
  return hash;
}

inline uint32_t alignUp(const uint32_t value) { return (value + CLIX_ALIGN - 1) / CLIX_ALIGN * CLIX_ALIGN; }

// Fill in every offset and the expected file size from the count and the name
// bytes alone, so the writer and the reader can never disagree about them.
inline void layoutSections(ClixHeader& h, const uint32_t nameBytes) {
  h.recordStart = CLIX_ALIGN;
  h.nameStart = alignUp(h.recordStart + static_cast<uint32_t>(h.bookCount) * sizeof(ClixRecord));
  h.nameLen = nameBytes;
  h.selfSize = h.nameStart + nameBytes;
}

inline uint32_t recordOffset(const ClixHeader& h, const uint16_t ordinal) {
  return h.recordStart + static_cast<uint32_t>(ordinal) * sizeof(ClixRecord);
}

enum class ClixValidity : uint8_t {
  Ok,
  BadMagic,
  UnknownFormatVersion,
  SizeMismatch,  // truncated, or written by something else
  CountOutOfRange,
  SectionsInconsistent,
};

// Validate a header against the real file size. Strict enough that nothing
// downstream has to re-check section bounds.
inline ClixValidity validateHeader(const ClixHeader& h, const uint64_t actualFileSize) {
  for (size_t i = 0; i < sizeof(CLIX_MAGIC); i++) {
    if (h.magic[i] != CLIX_MAGIC[i]) return ClixValidity::BadMagic;
  }
  if (h.formatVersion != CLIX_FORMAT_VERSION) return ClixValidity::UnknownFormatVersion;
  if (h.bookCount > CLIX_MAX_RECORDS) return ClixValidity::CountOutOfRange;
  if (actualFileSize != h.selfSize) return ClixValidity::SizeMismatch;
  // nameLen is a byte of the file: capped by the real size it cannot wrap the sum below.
  if (h.nameLen > actualFileSize) return ClixValidity::SectionsInconsistent;

  ClixHeader expected = h;
  layoutSections(expected, h.nameLen);
  if (expected.recordStart != h.recordStart || expected.nameStart != h.nameStart || expected.selfSize != h.selfSize) {
    return ClixValidity::SectionsInconsistent;
  }
  return ClixValidity::Ok;
}

// A record's text must lie inside the name section and within the bounds above.
inline bool recordInBounds(const ClixHeader& h, const ClixRecord& r) {
  const uint64_t end = static_cast<uint64_t>(r.nameOff) + r.titleLen + r.authorLen + r.pathLen;
  return end <= h.nameLen && r.titleLen <= CLIX_MAX_TITLE && r.authorLen <= CLIX_MAX_AUTHOR &&
         r.pathLen <= CLIX_MAX_PATH && r.pathLen > 0;
}

// --- text a book gave us -----------------------------------------------------

// Whitespace runs (and control bytes: tabs, newlines, NULs from a hostile OPF)
// become one space, the ends are trimmed, and the result is cut to `maxBytes`
// on a UTF-8 boundary. Never longer than the input.
inline std::string cleanText(const std::string_view in, const size_t maxBytes) {
  std::string out;
  out.reserve(std::min(in.size(), maxBytes));
  bool pendingSpace = false;
  for (const char c : in) {
    const auto u = static_cast<unsigned char>(c);
    if (u <= ' ' || u == 0x7F) {
      pendingSpace = !out.empty();
      continue;
    }
    const size_t need = (pendingSpace ? 1 : 0) + 1;
    if (out.size() + need > maxBytes) break;
    if (pendingSpace) out.push_back(' ');
    pendingSpace = false;
    out.push_back(c);
  }
  // A cut inside a multi-byte sequence leaves its lead byte with too few followers: drop it.
  size_t lead = out.size();
  while (lead > 0 && (static_cast<unsigned char>(out[lead - 1]) & 0xC0) == 0x80) lead--;
  if (lead > 0) {
    const auto b = static_cast<unsigned char>(out[lead - 1]);
    const size_t seq = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
    if (seq > 1 && out.size() - (lead - 1) < seq) out.resize(lead - 1);
  }
  return out;
}

// The title the list sorts by: ASCII lower-cased, everything else as it came.
// UTF-8 compares bytewise in code point order, so non-ASCII titles sort after
// ASCII ones. // ponytail: no accent or script folding, add when a card needs it.
inline std::string sortFold(const std::string_view title) {
  std::string out(title);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return out;
}

const char* clixValidityName(ClixValidity v);

}  // namespace library
