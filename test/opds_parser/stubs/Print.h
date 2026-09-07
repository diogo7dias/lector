#pragma once
// Minimal stand-in for Arduino's Print, enough for the parser under test.
#include <cstddef>
#include <cstdint>

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual void flush() {}
};
