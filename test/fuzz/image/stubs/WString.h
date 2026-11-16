#pragma once
// Arduino's String has no std::string constructor, so std::string arguments pick the
// std::string_view overloads, as on the device.
#include <cstddef>
#include <string>

class String {
 public:
  String(const char* s) : value(s ? s : "") {}
  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }

 private:
  std::string value;
};
