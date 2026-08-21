#pragma once
#include <HalStorage.h>
#include <Memory.h>

#include <algorithm>
#include <cstring>
#include <string>

// The length cap and the plausibility test, shared with the HalFile and stream readers.
#include "SerializationLimits.h"

namespace serialization {

// Sequential buffered wrappers over HalFile.
//
// SdFat keeps ONE shared 512-byte sector cache per volume, so interleaving small
// reads/writes across two or more files evicts and reloads that sector on nearly
// every call -- each 4-byte pod becomes a full SD transaction (measured: 31s to
// stream ~200KB through BookMetadataCache::buildBookBin on a 1,732-spine EPUB).
// Batching into chunk-sized transfers keeps each file at sequential SD speed.
//
// Heap: one fixed buffer per wrapper, allocated once at construction and freed at
// scope exit. If the allocation fails the wrapper degrades to unbuffered
// passthrough -- correct, just slow -- so callers never need an OOM path.
//
// Constraint: the wrapper must be the file's ONLY accessor while alive (it tracks
// the underlying position itself); mixing direct HalFile calls in desynchronizes it.

class BufferedFileWriter {
 public:
  BufferedFileWriter(HalFile& file, const size_t capacity)
      : file(file), buf(makeUniqueNoThrow<uint8_t[]>(capacity)), cap(buf ? capacity : 0), pos(file.position()) {}
  ~BufferedFileWriter() { flush(); }
  BufferedFileWriter(const BufferedFileWriter&) = delete;
  BufferedFileWriter& operator=(const BufferedFileWriter&) = delete;

  void write(const void* src, const size_t len) {
    pos += len;
    const auto* p = static_cast<const uint8_t*>(src);
    if (fill + len > cap) {
      flushBuffer();
    }
    if (len >= cap) {  // also the cap == 0 passthrough
      okFlag &= file.write(p, len) == len;
      return;
    }
    // Typed local: cppcheck misreads unique_ptr<uint8_t[]>::get() arithmetic as void*.
    uint8_t* const data = buf.get();
    memcpy(data + fill, p, len);
    fill += len;
  }

  // Logical write position (bytes written since the file was opened).
  size_t position() const { return pos; }

  // Flush buffered bytes; returns false if any write so far has failed short.
  bool flush() {
    flushBuffer();
    return okFlag;
  }

 private:
  void flushBuffer() {
    if (fill == 0) return;
    okFlag &= file.write(buf.get(), fill) == fill;
    fill = 0;
  }

  HalFile& file;
  std::unique_ptr<uint8_t[]> buf;
  const size_t cap;
  size_t fill = 0;
  size_t pos;
  bool okFlag = true;
};

class BufferedFileReader {
 public:
  BufferedFileReader(HalFile& file, const size_t capacity)
      : file(file), buf(makeUniqueNoThrow<uint8_t[]>(capacity)), cap(buf ? capacity : 0), bufStart(file.position()) {}
  BufferedFileReader(const BufferedFileReader&) = delete;
  BufferedFileReader& operator=(const BufferedFileReader&) = delete;

  size_t read(void* dst, size_t len) {
    auto* p = static_cast<uint8_t*>(dst);
    if (cap == 0) {  // passthrough
      const int n = file.read(p, len);
      const size_t got = n < 0 ? 0 : static_cast<size_t>(n);
      bufStart += got;
      return got;
    }
    size_t total = 0;
    while (len > 0) {
      if (off == fill) {
        bufStart += fill;
        off = 0;
        const int n = file.read(buf.get(), cap);
        fill = n < 0 ? 0 : static_cast<size_t>(n);
        if (fill == 0) break;  // EOF or error
      }
      const size_t chunk = std::min(len, fill - off);
      // Typed local: cppcheck misreads unique_ptr<uint8_t[]>::get() arithmetic as void*.
      const uint8_t* const data = buf.get();
      memcpy(p, data + off, chunk);
      p += chunk;
      off += chunk;
      len -= chunk;
      total += chunk;
    }
    return total;
  }

  // Logical read position.
  size_t position() const { return bufStart + off; }

  // Total size of the file being buffered, for bounding a length prefix read out of it.
  size_t size() { return file.size(); }

  bool seek(const size_t target) {
    // Within the buffered window: just move the cursor.
    if (cap != 0 && target >= bufStart && target < bufStart + fill) {
      off = target - bufStart;
      return true;
    }
    if (!file.seek(target)) return false;
    bufStart = target;
    fill = 0;
    off = 0;
    return true;
  }

 private:
  HalFile& file;
  std::unique_ptr<uint8_t[]> buf;
  const size_t cap;
  size_t fill = 0;
  size_t off = 0;
  size_t bufStart;
};

// serialization:: overloads mirroring the HalFile ones in Serialization.h.
template <typename T>
void writePod(BufferedFileWriter& out, const T& value) {
  out.write(&value, sizeof(T));
}

// Returns false on a short read, with the value zeroed rather than left holding stack
// garbage. Same contract as the HalFile overload in Serialization.h; see
// SerializationLimits.h for why an unchecked read here was a device crash.
template <typename T>
bool readPod(BufferedFileReader& in, T& value) {
  if (in.read(&value, sizeof(T)) != sizeof(T)) {
    memset(&value, 0, sizeof(T));
    return false;
  }
  return true;
}

inline void writeString(BufferedFileWriter& out, const std::string& s) {
  const uint32_t len = s.size();
  writePod(out, len);
  out.write(s.data(), len);
}

inline bool readString(BufferedFileReader& in, std::string& s) {
  s.clear();

  uint32_t len = 0;
  if (!readPod(in, len)) return false;

  const size_t here = in.position();
  const size_t total = in.size();
  if (total < here) return false;
  if (!stringLengthIsPlausible(len, static_cast<uint64_t>(total - here))) return false;
  if (len == 0) return true;

  s.resize(len);
  if (in.read(&s[0], len) != len) {
    s.clear();
    return false;
  }
  return true;
}

}  // namespace serialization
