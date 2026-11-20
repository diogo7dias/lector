#pragma once

// Builds the CLX1 index by walking the SD card.
//
// Shape of the build, and why:
//
//   * ONE walk. Each book goes straight into a staging file, in discovery order,
//     as a fixed 512-byte entry; nothing proportional to the library's TEXT stays
//     resident. What does: the previous index's keys (16 B per book, to find a
//     book again), the paths seen (8 B per book, to drop a duplicate directory
//     entry), and after the walk the sort keys (18 B per book). The first two are
//     freed before the third exists, so the peak is the walk's, ~16 KB per 1000
//     books beside the book parse, and ~18 KB per 1000 books for the sort.
//   * A book is parsed only when it is new or changed: same path, size and
//     modification time as in the previous index keeps its title and author, and a
//     book that was ever opened has them in its reader cache.
//   * Install is write-then-rename: the new index is written beside the old as
//     "<index>.part" and renamed into place whole. A reset mid-build leaves the old
//     index (or, between removing it and the rename, none), never a half-written
//     one, because only a complete file ever carries the final name.
//   * Duplicate directory entries are dropped. A damaged FAT can hand the same file
//     out twice, and without this the list shows a phantom row that cannot be opened.
//   * Unreadable books, and paths longer than CLIX_MAX_PATH, are skipped, never fatal.

#include <cstdint>
#include <string_view>

#include "LibraryFormat.h"

namespace library {

// Directory levels below the root that are walked. The cap exists because a
// corrupted FAT can hold a directory that contains itself.
inline constexpr int LIBRARY_MAX_DEPTH = 6;

// Longest run of titles sharing a 16-byte sort prefix that is ordered by full
// title. A longer run keeps the card's order inside itself.
inline constexpr size_t LIBRARY_MAX_TIE_RUN = 64;

struct BuildStats {
  uint16_t books = 0;
  uint16_t reused = 0;      // kept title and author from the previous index
  uint16_t parsed = 0;      // EPUB metadata reads this build performed
  uint16_t duplicates = 0;  // directory entries dropped as the same path twice
  uint16_t skipped = 0;     // names too long to store
  bool truncated = false;   // more than CLIX_MAX_RECORDS books on the card
  bool dedupDegraded = false;
};

// Called about every 16 directory entries so the caller can feed the watchdog and
// the busy banner during a walk of thousands of entries.
using ProgressFn = void (*)(void* ctx);

// Walk `rootPath` and write the index to `indexPath`. On false the previous
// index is untouched, unless the failure came between removing it and renaming
// the new one in, when there is none. `useMetadata` false skips opening EPUBs
// and lists every book by its filename stem with an empty author.
bool buildLibraryIndex(const char* rootPath, const char* indexPath, BuildStats& stats, ProgressFn progress = nullptr,
                       void* progressCtx = nullptr, bool useMetadata = true);

// Where the firmware keeps its index.
const char* libraryIndexPath();

// Whether the walk lists a file by this name: a book extension, not hidden, and
// not a "<book>_QUOTES.txt" the quote grabber writes beside a book.
bool isLibraryBookName(std::string_view name);

}  // namespace library
