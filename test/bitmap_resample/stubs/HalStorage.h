#pragma once
// Host-test stub: a HalFile over an in-memory byte vector, just the calls Bitmap makes.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

class HalFile {
 public:
  explicit HalFile(std::vector<uint8_t> data) : data_(std::move(data)) {}

  int read() { return position_ < data_.size() ? data_[position_++] : -1; }

  int read(void* buffer, size_t size) {
    const size_t count = std::min(size, data_.size() - std::min(position_, data_.size()));
    std::memcpy(buffer, data_.data() + position_, count);
    position_ += count;
    return static_cast<int>(count);
  }

  bool seek(size_t position) {
    if (position > data_.size()) return false;
    position_ = position;
    return true;
  }

  bool seekCur(int64_t offset) { return seek(static_cast<size_t>(static_cast<int64_t>(position_) + offset)); }

  explicit operator bool() const { return true; }

 private:
  std::vector<uint8_t> data_;
  size_t position_ = 0;
};
