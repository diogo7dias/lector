#include "LibraryFormat.h"

namespace library {

const char* clixValidityName(const ClixValidity v) {
  switch (v) {
    case ClixValidity::Ok:
      return "ok";
    case ClixValidity::BadMagic:
      return "bad magic";
    case ClixValidity::UnknownFormatVersion:
      return "unknown format version";
    case ClixValidity::SizeMismatch:
      return "size mismatch";
    case ClixValidity::CountOutOfRange:
      return "count out of range";
    case ClixValidity::SectionsInconsistent:
      return "sections inconsistent";
  }
  return "?";
}

}  // namespace library
