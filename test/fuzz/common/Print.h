#pragma once
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t b) = 0;
  virtual size_t write(const uint8_t* buf, size_t n) {
    size_t done = 0;
    while (done < n && write(buf[done])) done++;
    return done;
  }
};
