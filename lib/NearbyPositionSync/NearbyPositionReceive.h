#pragma once

#include <array>
#include <climits>
#include <optional>
#include <string_view>

#include "NearbyPositionProtocol.h"
#include "NearbyPositionResolve.h"

namespace nearby_position {

// Well-formed document hash: DOCUMENT_HASH_BYTES lowercase hex digits.
inline bool isDocumentHash(std::string_view hash) {
  if (hash.size() != DOCUMENT_HASH_BYTES) return false;
  for (char c : hash) {
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return true;
}

inline bool matchesDocumentHash(std::string_view offered, std::string_view candidate) {
  return isDocumentHash(offered) && offered == candidate;
}

// Validate before feeding the older XPath mapper, whose numeric parser uses int.
// A round-trip through the existing resolver must additionally prove the anchor.
inline bool receivablePosition(const PacketView& packet, std::string_view hash) {
  const auto& p = packet.position;
  if ((packet.type != PacketType::POSITION && packet.type != PacketType::APPLY) ||
      !matchesDocumentHash(std::string_view(p.documentHash.data(), DOCUMENT_HASH_BYTES), hash) ||
      p.documentHash.back() != '\0' || p.xpath.back() != '\0' || p.percentageQ > PERCENTAGE_SCALE ||
      p.totalPages == 0 || p.pageNumber >= p.totalPages)
    return false;
  const std::string_view xpath(p.xpath.data());
  if (xpath.substr(0, 18) != "/body/DocFragment[" || xpath.size() >= MAX_XPATH_BYTES) return false;
  unsigned number = 0;
  for (char c : xpath) {
    if (c >= '0' && c <= '9') {
      const unsigned digit = c - '0';
      if (number > (INT_MAX - digit) / 10u) return false;
      number = number * 10u + digit;
    } else {
      if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return false;
      number = 0;
    }
  }
  return true;
}

struct CachedPosition {
  uint16_t spine = 0;
  uint16_t page = 0;
  uint16_t pages = 0;
  std::optional<uint32_t> offset;
};

// The progress.bin format alone: 4, 6 or 10 little-endian bytes (spine, page[, pages
// [, visible-text offset]]). No judgement about the values; the reader and the sync
// sender each apply their own.
inline std::optional<CachedPosition> decodeCachedPosition(const uint8_t* bytes, size_t size) {
  if (!bytes || (size != 4 && size != 6 && size != 10)) return std::nullopt;
  const auto u16 = [bytes](size_t i) { return static_cast<uint16_t>(bytes[i] | (bytes[i + 1] << 8)); };
  CachedPosition p;
  p.spine = u16(0);
  p.page = u16(2);
  if (size >= 6) p.pages = u16(4);
  if (size == 10) {
    p.offset = uint32_t(bytes[6]) | (uint32_t(bytes[7]) << 8) | (uint32_t(bytes[8]) << 16) | (uint32_t(bytes[9]) << 24);
  }
  return p;
}

// A position worth sending to a peer: decoded, and neither the reader's navigation
// sentinel nor a page past the chapter's end.
inline std::optional<CachedPosition> readCachedPosition(const uint8_t* bytes, size_t size) {
  const auto p = decodeCachedPosition(bytes, size);
  if (!p || p->page == UINT16_MAX) return std::nullopt;
  if (p->pages != 0 && p->page >= p->pages) return std::nullopt;
  return p;
}

// Only receiver-local, verified page/offset pairs belong here; never wire page hints.
inline std::optional<std::array<uint8_t, 10>> receivedProgressRecord(std::string_view offeredHash,
                                                                     std::string_view matchedHash,
                                                                     const CachedPosition& mapped, uint16_t spineCount,
                                                                     bool anchorVerified, bool pageVerified,
                                                                     bool sectionMissing = false) {
  // A missing section forces the reader to rebuild and land by offset. An
  // existing section (including partial) requires a proven local page because
  // a cache hit discards the saved offset. Zero page count is saveProgress's
  // existing "unknown count" representation, not a new record format.
  if (!matchesDocumentHash(offeredHash, matchedHash) || !anchorVerified || !mapped.offset ||
      mapped.spine >= spineCount || mapped.page == UINT16_MAX || (mapped.pages != 0 && mapped.page >= mapped.pages) ||
      (!pageVerified && !(sectionMissing && mapped.page == 0 && mapped.pages == 0)))
    return std::nullopt;
  return std::array<uint8_t, 10>{uint8_t(mapped.spine),         uint8_t(mapped.spine >> 8),
                                 uint8_t(mapped.page),          uint8_t(mapped.page >> 8),
                                 uint8_t(mapped.pages),         uint8_t(mapped.pages >> 8),
                                 uint8_t(*mapped.offset),       uint8_t(*mapped.offset >> 8),
                                 uint8_t(*mapped.offset >> 16), uint8_t(*mapped.offset >> 24)};
}

inline Resolution resolveCachedPosition(const CachedPosition& local, const CachedPosition& peer) {
  // Both offsets were resolved on the same document. This avoids comparing
  // percentages derived from two different devices' pagination.
  const bool same = local.spine == peer.spine && local.offset == peer.offset;
  const bool further = peer.spine > local.spine || (peer.spine == local.spine && peer.offset > local.offset);
  return resolvePosition(same, further);
}

}  // namespace nearby_position
