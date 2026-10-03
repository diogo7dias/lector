#pragma once
// Arduino's Print.h brings String in with it; HalStorage.h relies on that.
#include <cstddef>
#include <cstdint>

#include "WString.h"

class Print {
 public:
  virtual ~Print() = default;
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) = 0;
  virtual void flush() {}
};
