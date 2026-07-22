#pragma once

#include <cstddef>

// Minimal host stub: FsHelpers.h includes <WString.h> for its String overloads,
// but WebPath and the normalisePath path we exercise never call String methods,
// so a declaration-only shim is enough to compile + link.
class String {
 public:
  const char* c_str() const;
  size_t length() const;
};
