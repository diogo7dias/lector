#pragma once

#include <cstdint>

// Anchor identity, as an add-rotate-xor hash rather than the anchor text itself.
//
// Anchors are only ever compared for equality — nothing displays them — so the string is
// dead weight after the first comparison. A chapter can carry MAX_ANCHORS_PER_CHAPTER of
// them, and holding each as a std::string costs 32 B of vector slot plus a separate heap
// block for anything past the 15-char small-string buffer. At the cap that is ~32 KB of
// slots and up to 1024 individual allocations scattered through the heap during a parse,
// which is one of the ways a big chapter fails to lay out on a 380 KB device. A uint64_t
// is 8 B in a flat array and allocates nothing.
//
// WARNING: this function defines the on-disk anchor map. Changing the mixing here changes
// every stored key, so it MUST come with a SECTION_FILE_VERSION bump in Section.cpp — a
// stale map does not fail loudly, it silently sends footnote links to the wrong page.
inline uint64_t arxHash64(const char* s) {
  uint64_t hash = 0xcbf29ce484222325ULL;
  while (const char c = *s++) {
    hash ^= static_cast<uint8_t>(c);
    hash = (hash << 19) | (hash >> 45);
    hash += 0x9e3779b97f4a7c15ULL;
  }
  return hash;
}
