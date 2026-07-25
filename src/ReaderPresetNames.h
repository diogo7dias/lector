#pragma once

// Pure name rules for reader presets ("Reading Themes" in the UI).
// No Arduino types, no Storage — safe for host-side testing (test/reader_presets).

#include <string>
#include <vector>

namespace readerpreset {

// A name has to fit one list row and one keyboard screen.
inline constexpr size_t MAX_NAME_LENGTH = 20;

// Trim surrounding blanks and clip to MAX_NAME_LENGTH. Returns empty when the input
// was blank, which the caller treats as "use the suggestion".
std::string sanitizeName(const std::string& raw);

// A name not already taken, comparing case-insensitively so "Night" and "night" are
// not both offered. Appends " 2", " 3" … and keeps the result within MAX_NAME_LENGTH
// by trimming the base, not the suffix — a truncated suffix would collide again.
// `skipIndex` is the entry allowed to keep its own name (for rename); pass -1 to
// consider every existing name taken.
std::string makeUniqueName(const std::string& desired, const std::vector<std::string>& existing, int skipIndex = -1);

}  // namespace readerpreset
