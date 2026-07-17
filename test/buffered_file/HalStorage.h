#pragma once

// Host-test stub for the firmware HalStorage/HalFile. TextBlock.cpp pulls in
// <Serialization.h> (whose templates need a HalFile with read/write/position/
// size) even though the layout tests never serialize. An in-memory byte
// buffer satisfies the whole surface.

#include <cstdint>
#include <cstring>
#include <vector>

class HalFile {
 public:
  int write(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
    pos_ = buf_.size();
    return static_cast<int>(len);
  }
  int read(void* dst, size_t len) {
    const size_t avail = buf_.size() > pos_ ? buf_.size() - pos_ : 0;
    const size_t n = len < avail ? len : avail;
    std::memcpy(dst, buf_.data() + pos_, n);
    pos_ += n;
    return static_cast<int>(n);
  }
  size_t position() const { return pos_; }
  size_t size() const { return buf_.size(); }
  bool seek(size_t p) {
    if (p > buf_.size()) return false;
    pos_ = p;
    return true;
  }
  void flush() {}
  void close() {}
  explicit operator bool() const { return true; }

 private:
  std::vector<uint8_t> buf_;
  size_t pos_ = 0;
};
