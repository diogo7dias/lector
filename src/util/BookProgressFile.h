#pragma once

#include <cstdint>
#include <string>

// The small "how far in, and how recently" marker a reader leaves in a book's cache
// directory, in `percent.bin`.
//
// The home screen's badge comes from the recents list, which holds thirteen books and
// forgets one the moment it is removed or filed away as read. The file browser wants the
// same information for every book that was ever opened, so it is kept next to the book's
// own cache instead: five bytes, written once per reading session, read back per visible
// row. A book that was never opened has no cache directory and therefore no marker, which
// is exactly the "no badge" case.
//
// The read order is a counter rather than a timestamp: this firmware registers no SdFat
// date/time callback, so every file it writes carries the same constant FAT stamp and the
// card cannot say which book was read most recently. The counter is handed out by
// CrossPointState, one per reading session, and only its order is ever compared.
//
// Sibling of activities/reader/ProgressFile.h, which writes `progress.bin` into the same
// directory: that one holds the reading POSITION and is rewritten on a debounce, this one
// holds what the library screens show ABOUT the book. This file is written in place rather
// than through a temp-and-rename: a torn write leaves a short or zero-length record, which
// reads back as "no badge" instead of a wrong one, so the ceremony would buy nothing.
namespace book_progress {

struct Marker {
  uint8_t percent = 0;     // 0-100; 100 means the book is finished
  uint32_t readOrder = 0;  // higher was read more recently; 0 = unknown
};

// Writes the marker into `<cacheDir>/percent.bin`. Does nothing, reporting success, when
// the cache directory is empty, so a caller need not special-case a format with no cache.
bool write(const std::string& cacheDir, const Marker& marker);

// Reads the marker for the book at `bookPath`. Returns false when the book has no cache
// directory, no marker, or an unreadable one — all of which mean "draw no badge".
bool readForBook(const std::string& bookPath, Marker& markerOut);

// Seeds markers from the recents list for books read before this firmware existed, so an
// upgraded device shows badges without every book having to be reopened first. Recents
// holds thirteen entries with a stored percentage, so this is at most thirteen small
// writes, and it is meant to be run once (the caller keeps the "done" flag).
//
// The seeded entries carry no read order: nothing recorded when they were read, and
// inventing an order would sort them against sessions that really were measured.
void backfillFromRecents();

}  // namespace book_progress
