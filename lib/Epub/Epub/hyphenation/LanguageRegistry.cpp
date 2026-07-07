#include "LanguageRegistry.h"

#include <algorithm>
#include <array>

#include "HyphenationCommon.h"
#include "generated/hyph-en.trie.h"
#include "generated/hyph-pt.trie.h"

// Only English + Portuguese hyphenation are baked in (the languages actually
// read on this device). The other tries (de/es/fr/it/pl/ru/sv/uk) were dropped
// to reclaim ~322 KB of flash; books in those languages simply render without
// hyphenation (getLanguageHyphenatorForPrimaryTag returns nullptr for them).

namespace {

// English hyphenation patterns (3/3 minimum prefix/suffix length)
LanguageHyphenator englishHyphenator(en_patterns, isLatinLetter, toLowerLatin, 3, 3);
LanguageHyphenator portugueseHyphenator(pt_patterns, isLatinLetter, toLowerLatin);

using EntryArray = std::array<LanguageEntry, 2>;

const EntryArray& entries() {
  static const EntryArray kEntries = {
      {{"english", "en", &englishHyphenator}, {"portuguese", "pt", &portugueseHyphenator}}};
  return kEntries;
}

}  // namespace

const LanguageHyphenator* getLanguageHyphenatorForPrimaryTag(const std::string& primaryTag) {
  const auto& allEntries = entries();
  const auto it = std::find_if(allEntries.begin(), allEntries.end(),
                               [&primaryTag](const LanguageEntry& entry) { return primaryTag == entry.primaryTag; });
  return (it != allEntries.end()) ? it->hyphenator : nullptr;
}

LanguageEntryView getLanguageEntries() {
  const auto& allEntries = entries();
  return LanguageEntryView{allEntries.data(), allEntries.size()};
}
