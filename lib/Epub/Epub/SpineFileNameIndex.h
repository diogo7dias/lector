#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>

// Resolves a TOC entry's target document to a spine index by FILE NAME.
//
// The exact path is the answer whenever the spine carries it, and the caller tries that
// first. It often does not: a TOC document resolves its hrefs against its own folder, so
// the path built from it can name the right file through a different directory prefix
// than the manifest used, and an unresolvable entry leaves the reader with a chapter row
// that does nothing at all. The file name then decides — but only when exactly one spine
// item carries it, because two folders holding an index.html make any guess a wrong
// chapter.
//
// Built ONCE per book, from a single pass over the spine, and only when an entry actually
// needs it. Resolving each entry with its own pass instead is what made a book whose
// whole TOC uses a different prefix read the entire spine off the SD card for every row.
//
// Names are kept as a hash plus a length rather than as strings: the spine is read one
// entry at a time from the card precisely because holding every href in RAM is what this
// device does not have. That is the same trade the exact-href index already makes.
//
// Cost is 16 bytes per spine item, live only between the first entry that needs it and
// endTocPass, and only for a book that needs it at all. On a large book that is the same
// order as the exact-href index this sits beside (6.4 KB each at the 400-item threshold).
// The alternative it replaces was a full pass over the spine PER TOC row, which on the
// books this exists for meant hundreds of thousands of SD reads.
//
// This resolves by FILE NAME only. Exact-path precedence lives in the caller, which tries
// the exact lookup first and only reaches here when the spine has no such path.
class SpineFileNameIndex {
 public:
  static std::string_view fileNameOf(const std::string_view path) {
    const size_t slash = path.find_last_of('/');
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
  }

  // FNV-1a 64-bit. Same offset basis and prime as BookMetadataCache::fnvHash64, so the
  // two indexes stay comparable if they are ever merged.
  static uint64_t hashOf(const std::string_view text) {
    uint64_t hash = 14695981039346656037ULL;
    for (const char c : text) {
      hash ^= static_cast<uint8_t>(c);
      hash *= 1099511628211ULL;
    }
    return hash;
  }

  void reserve(const size_t count) { entries_.resize(count); }

  // Call once per spine item, in spine order.
  void add(const int16_t spineIndex, const std::string& spineHref) {
    const std::string_view name = fileNameOf(spineHref);
    Entry entry;
    entry.nameHash = hashOf(name);
    entry.nameLen = static_cast<uint16_t>(name.size());
    entry.spineIndex = spineIndex;
    if (added_ < entries_.size()) {
      entries_[added_] = entry;
    } else {
      entries_.push_back(entry);
    }
    added_++;
  }

  // Call once, after the last add().
  void seal() {
    entries_.resize(added_);
    std::sort(entries_.begin(), entries_.end(), [](const Entry& a, const Entry& b) {
      return a.nameHash < b.nameHash || (a.nameHash == b.nameHash && a.nameLen < b.nameLen);
    });
    sealed_ = true;
  }

  bool sealed() const { return sealed_; }

  void clear() {
    entries_.clear();
    entries_.shrink_to_fit();
    added_ = 0;
    sealed_ = false;
  }

  // Spine index for `tocHref`, or -1 when the book gives no unambiguous answer.
  int16_t resolve(const std::string& tocHref) const {
    const std::string_view name = fileNameOf(tocHref);
    if (name.empty()) return -1;
    const uint64_t hash = hashOf(name);
    const auto len = static_cast<uint16_t>(name.size());

    Entry probe;
    probe.nameHash = hash;
    probe.nameLen = len;
    auto it = std::lower_bound(entries_.begin(), entries_.end(), probe, [](const Entry& a, const Entry& b) {
      return a.nameHash < b.nameHash || (a.nameHash == b.nameHash && a.nameLen < b.nameLen);
    });

    if (it == entries_.end() || it->nameHash != hash || it->nameLen != len) return -1;
    const int16_t answer = it->spineIndex;
    ++it;
    // A second spine item carries the same name: guessing would send the reader to the
    // wrong chapter, so the entry is left unresolved instead.
    if (it != entries_.end() && it->nameHash == hash && it->nameLen == len) return -1;
    return answer;
  }

 private:
  struct Entry {
    uint64_t nameHash = 0;
    uint16_t nameLen = 0;
    int16_t spineIndex = -1;
  };
  // deque, not vector: the index is built during the cache pass, when the heap is already
  // carrying the spine and TOC buffers, and a vector's 2x growth would need a contiguous
  // block twice the final size at the moment it reallocates.
  std::deque<Entry> entries_;
  size_t added_ = 0;
  bool sealed_ = false;
};
