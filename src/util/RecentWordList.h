#pragma once

#include <algorithm>
#include <string>
#include <vector>

// The "newest first, no duplicates, capped" rule the dictionary history follows, kept apart
// from the store that persists it so the rule itself can be host-tested.
namespace recent_words {

// Moves `word` to the front, dropping any earlier copy of it and any word past `cap`.
// Returns false when nothing moved (the word is already the newest, or it is empty), so the
// caller can skip a pointless SD write.
inline bool moveToFront(std::vector<std::string>& words, const std::string& word, const size_t cap) {
  if (word.empty() || cap == 0) return false;
  if (!words.empty() && words.front() == word) return false;

  const auto existing = std::find(words.begin(), words.end(), word);
  if (existing != words.end()) words.erase(existing);

  words.insert(words.begin(), word);
  if (words.size() > cap) words.resize(cap);
  return true;
}

}  // namespace recent_words
