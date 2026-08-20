#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

// A bounded, nothrow list of file names.
//
// Replaces std::vector<std::string> for directory listings. A wallpaper or library
// folder can hold thousands of entries; one std::string per entry costs ~72 bytes
// (32 for the object in the vector plus a heap block for the name) and the throwing
// operator new behind it aborts the firmware on a full heap, because the build has
// exceptions disabled. Thousands of images therefore killed the file browser rather
// than showing a short list.
//
// Here every name lives in one growable character blob with a NUL after each, and the
// list is a table of offsets into it. That costs about name length + 5 bytes per entry.
// Growth is makeUniqueNoThrow, so exhaustion truncates the listing instead of aborting,
// and two explicit budgets bound the whole structure regardless of folder size.
class NameList {
 public:
  // Whichever budget trips first stops the listing. 48 KB of names plus a 4000-entry
  // offset table (16 KB) is ~64 KB worst case, which fits beside the framebuffer with
  // room to spare, and 4000 entries is far past what is navigable by button anyway.
  static constexpr uint32_t MAX_BLOB_BYTES = 48u * 1024u;
  static constexpr uint32_t MAX_ENTRIES = 4000u;

  bool push(std::string_view name) {
    if (offsets_.size() >= MAX_ENTRIES) {
      truncated_ = true;
      return false;
    }
    const uint32_t need = static_cast<uint32_t>(name.size()) + 1;
    if (blobLen_ + need > MAX_BLOB_BYTES) {
      truncated_ = true;
      return false;
    }
    if (!ensureCapacity(blobLen_ + need)) {
      truncated_ = true;
      return false;
    }
    // offsets_ is the one allocation left that can throw. Reserving in step with the
    // blob keeps its growth amortised, and the MAX_ENTRIES cap bounds it outright.
    if (offsets_.capacity() == offsets_.size() && !reserveOffsets(offsets_.size() + 64)) {
      truncated_ = true;
      return false;
    }
    std::memcpy(blob_.get() + blobLen_, name.data(), name.size());
    blob_[blobLen_ + name.size()] = '\0';
    offsets_.push_back(blobLen_);
    blobLen_ += need;
    return true;
  }

  size_t size() const { return offsets_.size(); }
  bool empty() const { return offsets_.empty(); }

  std::string_view operator[](size_t i) const { return std::string_view(blob_.get() + offsets_[i]); }

  // The name as a NUL-terminated C string. Safe because every entry is stored with its
  // terminator, so this may be handed to the C APIs (paths, drawText) directly.
  const char* cstr(size_t i) const { return blob_.get() + offsets_[i]; }

  // True when a budget stopped the listing, so the caller can say so on screen rather
  // than silently showing a partial folder.
  bool truncated() const { return truncated_; }

  void clear() {
    offsets_.clear();
    blobLen_ = 0;
    truncated_ = false;
  }

  // Sorting and shuffling move offsets, never the characters they point at.
  // The comparator takes NUL-terminated names so sorting a folder allocates nothing;
  // building a std::string per comparison would defeat the point of the arena.
  template <typename Less>
  void sortByC(Less less) {
    std::sort(offsets_.begin(), offsets_.end(),
              [&](const uint32_t a, const uint32_t b) { return less(blob_.get() + a, blob_.get() + b); });
  }

  // Reorders every entry by a caller-supplied key, largest key first, with `less` breaking
  // ties so equal keys keep a stable, meaningful order (the file browser passes its name
  // comparator, which keeps same-key books alphabetical). `keys` holds one key per entry in
  // the CURRENT order, so it must be built against this list and used before anything else
  // moves the offsets.
  //
  // Sorts an index permutation rather than the offsets themselves: the key has to travel
  // with its entry, and re-deriving it inside the comparator would mean an SD read per
  // comparison. The scratch is one uint32_t per entry and is nothrow, so a folder too big
  // to allocate for keeps the order it already had instead of aborting the firmware.
  template <typename Less>
  bool sortByKeyDesc(const uint32_t* keys, Less less) {
    const size_t count = offsets_.size();
    if (count < 2) return true;

    auto order = makeUniqueNoThrow<uint32_t[]>(count);
    auto sorted = makeUniqueNoThrow<uint32_t[]>(count);
    if (!order || !sorted) return false;
    for (size_t i = 0; i < count; i++) order[i] = static_cast<uint32_t>(i);

    std::sort(order.get(), order.get() + count, [&](const uint32_t a, const uint32_t b) {
      if (keys[a] != keys[b]) return keys[a] > keys[b];
      return less(blob_.get() + offsets_[a], blob_.get() + offsets_[b]);
    });

    for (size_t i = 0; i < count; i++) sorted[i] = offsets_[order[i]];
    for (size_t i = 0; i < count; i++) offsets_[i] = sorted[i];
    return true;
  }

  // Fisher-Yates over [first, size), with the caller's random source so the firmware can
  // pass the hardware RNG and the host tests can pass a deterministic one.
  template <typename Rand>
  void shuffleTail(size_t first, Rand rand) {
    if (size() - first < 2 || first >= size()) return;
    for (size_t i = size() - 1; i > first; i--) {
      const size_t j = first + (rand() % (i - first + 1));
      std::swap(offsets_[i], offsets_[j]);
    }
  }

 private:
  bool ensureCapacity(uint32_t needed) {
    if (needed <= blobCap_) return true;
    uint32_t next = blobCap_ ? blobCap_ : 1024;
    while (next < needed) next += next / 2 + 1;
    if (next > MAX_BLOB_BYTES) next = MAX_BLOB_BYTES;
    if (next < needed) return false;
    auto grown = makeUniqueNoThrow<char[]>(next);
    if (!grown) return false;
    if (blobLen_) std::memcpy(grown.get(), blob_.get(), blobLen_);
    blob_ = std::move(grown);
    blobCap_ = next;
    return true;
  }

  bool reserveOffsets(size_t n) {
    if (n > MAX_ENTRIES) n = MAX_ENTRIES;
    // std::vector::reserve throws on failure, which aborts here. Probe the allocation
    // with the nothrow path first and only reserve once it is known to fit.
    auto probe = makeUniqueNoThrow<uint32_t[]>(n);
    if (!probe) return false;
    probe.reset();
    offsets_.reserve(n);
    return true;
  }

  std::unique_ptr<char[]> blob_;
  uint32_t blobCap_ = 0;
  uint32_t blobLen_ = 0;
  std::vector<uint32_t> offsets_;
  bool truncated_ = false;
};
