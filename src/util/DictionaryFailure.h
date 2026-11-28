#pragma once

#include <I18n.h>

#include <string>
#include <vector>

#include "util/Dictionary.h"
#include "util/DictionaryRegistry.h"

// Which message a failed lookup shows, in one place, so the two screens that look words up
// (the page-side word select and the touch selection) cannot drift into naming the same
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

// The lookup every screen runs: open the chosen dictionary, index it first when it has no
// index yet, then look `word` up. `showBusy(ctx, message)` paints the wait before each slow
// step. On a miss `failure` names it, so no screen has to map the results itself.
inline bool lookUp(Dictionary& dict, const char* folder, const char* word, std::string& definition,
                   std::string& headword, Message& failure, void (*showBusy)(void*, StrId), void* ctx) {
  const Dictionary::Readiness readiness = dict.prepare(folder);
  const bool needsIndex = readiness == Dictionary::Readiness::NeedsIndex;
  showBusy(ctx, needsIndex ? StrId::STR_DICT_INDEXING : StrId::STR_DICT_LOOKING_UP);

  bool ok = readiness != Dictionary::Readiness::Unavailable;
  Dictionary::IndexResult indexResult = Dictionary::IndexResult::Ok;
  if (needsIndex) {
    ok = dict.buildIndex(&indexResult);
    // The build is over; the wait that remains is the lookup, so say so.
    if (ok) showBusy(ctx, StrId::STR_DICT_LOOKING_UP);
  }

  Dictionary::LookupResult result = Dictionary::LookupResult::NotFound;
  if (ok && dict.lookup(word, definition, headword, &result)) return true;
  failure = ok ? forLookup(result) : forIndex(indexResult);
  return false;
}

// Try `primaryFolder` first; on a genuine NotFound (never masking an error on the
// primary dictionary), try every other folder in `fallbacks` in turn, reusing the
// single Dictionary object.
inline bool lookUpInOrder(Dictionary& dict, const char* primaryFolder, const std::vector<DictionaryEntry>& fallbacks,
                          const char* word, std::string& definition, std::string& headword, Message& failure,
                          void (*showBusy)(void*, StrId), void* ctx) {
  if (lookUp(dict, primaryFolder, word, definition, headword, failure, showBusy, ctx)) return true;
  if (failure.isError) return false;
  const char* primary = primaryFolder ? primaryFolder : "";
  for (const auto& entry : fallbacks) {
    if (entry.name == primary) continue;
    Message subFailure;
    if (lookUp(dict, entry.name.c_str(), word, definition, headword, subFailure, showBusy, ctx)) return true;
  }
  failure = {StrId::STR_DICT_NOT_FOUND, false};
  return false;
}

inline bool lookUpWithFallbacks(Dictionary& dict, const char* primaryFolder, const bool searchFallbacks,
                                const char* word, std::string& definition, std::string& headword, Message& failure,
                                void (*showBusy)(void*, StrId), void* ctx) {
  if (lookUp(dict, primaryFolder, word, definition, headword, failure, showBusy, ctx)) return true;
  if (!searchFallbacks || failure.isError) return false;
  std::vector<DictionaryEntry> all;
  DictionaryRegistry::discover(all);
  const char* primary = primaryFolder ? primaryFolder : "";
  for (const auto& entry : all) {
    if (entry.name == primary) continue;
    Message subFailure;
    if (lookUp(dict, entry.name.c_str(), word, definition, headword, subFailure, showBusy, ctx)) return true;
  }
  failure = {StrId::STR_DICT_NOT_FOUND, false};
  return false;
}

}  // namespace dict_failure
