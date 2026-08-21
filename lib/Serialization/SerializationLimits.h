#pragma once

// Bounded reads for the on-card binary formats.
//
// A cache file is written by this firmware, but it is read back off a removable card,
// possibly days later, possibly after a write that a pulled card cut in half, possibly by
// a different firmware version than the one that wrote it. It is not trusted input.
//
// A string is stored as a 32-bit length followed by that many bytes, and the length used
// to go straight into std::string::resize(). A corrupt length therefore asked the
// allocator for an arbitrary size. On this target an unhandled std::bad_alloc is not an
// exception a caller can catch — it is std::terminate, which is abort(), which is a device
// crash. One damaged byte in a TOC entry took the reader down through
// Epub::getTocItem -> BookMetadataCache::readTocEntry -> readString -> operator new.
//
// So every read here reports whether it succeeded, and a length that cannot possibly be
// right is refused before anything is allocated. A refused read leaves the destination
// empty rather than half-filled, so a caller that checks the return value gets a clean
// "this entry is unreadable" and a caller that does not gets an empty string instead of a
// crash.
//
// This header deliberately has no HalStorage/Arduino dependency: the stream overloads are
// the same logic the card path uses and are host-tested in test/serialization_bounds.

#include <cstdint>
#include <cstring>
#include <istream>
#include <string>

namespace serialization {

// The largest string these formats can legitimately hold. Every string stored is a title,
// an href, an anchor, or an identifier; none of them come close. The cap is what makes a
// corrupt length cheap to reject — it is checked before the bytes-remaining test so a
// nonsense value never has to be reasoned about against a file size.
constexpr uint32_t kMaxStringBytes = 8192;

// Whether a length prefix read off a file is safe to hand to resize(). `bytesRemaining` is
// what is actually left to read, so a length that survives the cap but still runs past the
// end of the data is rejected as well.
constexpr bool stringLengthIsPlausible(const uint32_t len, const uint64_t bytesRemaining) {
  return len <= kMaxStringBytes && len <= bytesRemaining;
}

// Returns false when the stream could not supply a whole T. The value is zeroed on
// failure rather than left holding whatever the caller's stack had, which is what made a
// short read produce a wild length instead of an obvious one.
template <typename T>
bool readPod(std::istream& is, T& value) {
  is.read(reinterpret_cast<char*>(&value), sizeof(T));
  if (is.gcount() != static_cast<std::streamsize>(sizeof(T))) {
    std::memset(&value, 0, sizeof(T));
    return false;
  }
  return true;
}

inline bool readString(std::istream& is, std::string& s) {
  s.clear();

  uint32_t len = 0;
  if (!readPod(is, len)) return false;

  // How much is left. Seeking to the end and back is the only way to ask an istream this;
  // on the card path the file knows its own size, so no seek is needed there.
  const std::streampos here = is.tellg();
  if (here < 0) return false;
  is.seekg(0, std::ios::end);
  const std::streampos end = is.tellg();
  is.seekg(here);
  if (end < here) return false;
  const uint64_t remaining = static_cast<uint64_t>(end - here);

  if (!stringLengthIsPlausible(len, remaining)) return false;
  if (len == 0) return true;

  s.resize(len);
  is.read(&s[0], len);
  if (is.gcount() != static_cast<std::streamsize>(len)) {
    s.clear();
    return false;
  }
  return true;
}

}  // namespace serialization
