#pragma once

#include <I18n.h>

#include "util/Dictionary.h"

// Which message a failed lookup shows, in one place, so the two screens that look words up
// (the page-side word select and the history list) cannot drift into naming the same
// failure differently. A word that was found but could not be read is a real error and says
// so; only a genuine miss says "Not found", because a "Not found" the user cannot act on is
// worse than an error they can.
namespace dict_failure {

// True when the message describes a failure rather than a plain miss. The caller uses it to
// pick which popup style to show; the message itself is the same either way.
struct Message {
  StrId id = StrId::STR_DICT_NOT_FOUND;
  bool isError = false;
};

// The dictionary could not be opened, or its index could not be built.
inline Message forIndex(const Dictionary::IndexResult indexResult) {
  switch (indexResult) {
    // An index build allocates a scan buffer, so it fails the same way lookups do on a
    // fragmented heap — name that rather than a generic error.
    case Dictionary::IndexResult::LowMemory:
      return {StrId::STR_DICT_LOW_MEMORY, true};
    case Dictionary::IndexResult::ReadError:
      return {StrId::STR_DICT_READ_FAILED, true};
    case Dictionary::IndexResult::Ok:
    default:
      return {StrId::STR_DICT_ERROR, true};  // the open failed, not the index
  }
}

// The dictionary was usable but the lookup did not return a definition.
inline Message forLookup(const Dictionary::LookupResult result) {
  switch (result) {
    case Dictionary::LookupResult::Decompress:
      return {StrId::STR_DICT_DECOMPRESS_ERROR, true};
    case Dictionary::LookupResult::LowMemory:
      return {StrId::STR_DICT_LOW_MEMORY, true};
    case Dictionary::LookupResult::ReadError:
      return {StrId::STR_DICT_READ_FAILED, true};
    case Dictionary::LookupResult::NotFound:
    default:
      return {StrId::STR_DICT_NOT_FOUND, false};
  }
}

}  // namespace dict_failure
