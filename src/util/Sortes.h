#pragma once

#include <string>
#include <string_view>

#include "SortesSelection.h"

namespace sortes {
enum class ScanResult { Found, Empty, Failed };

struct UnreadPick {
  std::string path;
  std::string title;
  std::string author;
};

// Includes every finished EPUB in the library, reading CLX v2 sequentially when
// present and falling back to a directory walk when no index exists yet.
ScanResult findBook(std::string& selected, Random random);

// Draws a random unread book (percent == CLIX_PERCENT_UNREAD) from CLX v2,
// falling back to a directory walk when no index exists yet.
ScanResult findUnreadBook(UnreadPick& pick, Random random);

// Looks up a book path by title (case-insensitive) in CLX v2, for Flashback's
// Jump to Book action.
bool findBookByTitle(std::string_view title, std::string& pathOut);
}  // namespace sortes
