#pragma once

#include <string>

#include "util/BookFilingNames.h"

// Where a book file lives on the card, and how it moves between those places.
//
// Opening a book files it into "/recents"; finishing it files it into "/read".
// Removing it from the recent list undoes that, putting the file back at the card
// root. All three are the same move, so they share one place here rather than each
// screen growing its own copy.
//
// The path arithmetic lives in BookFilingNames.h and is host-tested
// (test/book_filing). Everything here touches the SD card, the recents store and
// the app state.
namespace bookfiling {

// The path a book should move to inside `folder`, skipping names already taken on
// the card. Creates `folder` unless it is the root.
std::string buildFolderDestination(const std::string& srcPath, const char* folder);

// Move a book and its cache dir to dstPath, repointing its recents entry and the
// resume pointer. The caller must release the Epub first: renaming a file with an
// open handle is what every call site here carefully avoids.
// `cachePathHint` is an open book's own cache path; leave it empty and the cache dir
// is derived from the name instead.
// Returns the path the book ends up at — dstPath on success, srcPath if the rename
// failed (logged, nothing else disturbed).
std::string moveBookToFolder(const std::string& srcPath, const std::string& dstPath,
                             const std::string& cachePathHint = "");

// Undo the filing that opening a book did: if it sits in "/recents", move it back to
// the card root. Returns the path it ends up at, which is srcPath when it was not
// filed there or the move failed. Does NOT touch the recent list — the caller
// removes the entry, using the returned path.
std::string unfileFromRecents(const std::string& srcPath, const std::string& cachePathHint = "");

}  // namespace bookfiling
