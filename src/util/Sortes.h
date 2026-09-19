#pragma once

#include <string>

#include "SortesSelection.h"

namespace sortes {
enum class ScanResult { Found, Empty, Failed };
// Includes every finished EPUB in the library, independent of the recents cap.
ScanResult findBook(std::string& selected, Random random);
}  // namespace sortes
