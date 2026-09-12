#pragma once

#include <string>
#include <string_view>

class String {
 public:
  String() = default;
  String(const char* s) : value(s ? s : "") {}
  String(const std::string& s) : value(s) {}

  const char* c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }

 private:
  std::string value;
};
