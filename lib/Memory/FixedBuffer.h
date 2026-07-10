#pragma once

#include <array>
#include <cstddef>

template <typename T, size_t Capacity>
class FixedBuffer final {
 public:
  constexpr T* data() { return storage_.data(); }
  constexpr const T* data() const { return storage_.data(); }
  static constexpr size_t size() { return Capacity; }

 private:
  std::array<T, Capacity> storage_{};
};
