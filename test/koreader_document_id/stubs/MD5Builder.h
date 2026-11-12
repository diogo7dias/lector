#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class MD5Builder {
 public:
  void begin() { total = 0; }
  void add(const uint8_t*, size_t len) { total += len; }
  void add(const char* text) { total += std::string(text).size(); }
  void calculate() {}
  std::string toString() const { return std::to_string(total); }

 private:
  size_t total = 0;
};
