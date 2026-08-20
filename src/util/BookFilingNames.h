#pragma once

// Pure path helpers for filing a book into (or out of) a folder on the card.
// No Arduino types, no APP_STATE, no Storage — safe for host-side testing.
// BookFiling.h re-exports these; BookFiling.cpp includes this header.

#include <string>
#include <string_view>

namespace bookfiling {

// The card root as a folder argument: empty, so a destination reads "/name.epub"
// and there is no directory to create first.
inline constexpr const char* ROOT_FOLDER = "";
inline constexpr const char* READ_FOLDER = "/read";
inline constexpr const char* RECENTS_FOLDER = "/recents";

// True if path is directly inside `folder` (starts with "<folder>/"). Takes views so
// it allocates nothing and is cheap to call from loop().
bool isInFolder(std::string_view path, std::string_view folder);

// "/recents/My Book.epub" -> "My Book.epub". The whole string when there is no slash.
std::string_view fileNameOf(std::string_view path);

// What to call a book on screen: its own title when it has a usable one, its file name
// otherwise. A title straight out of an OPF file can be surrounded by the newlines and
// indentation of a pretty-printed document, so it is trimmed before it is judged empty —
// otherwise a whitespace-only title names the book with a blank line, which on a delete
// confirmation means the prompt identifies nothing.
std::string displayNameFor(std::string_view title, std::string_view path);

// Candidate destinations inside `folder`, in the order they should be tried:
// index 1 is "<folder>/name.epub", index 2 "<folder>/name (2).epub", and so on.
// Splitting this out of the collision loop is what makes the naming testable.
std::string destinationCandidate(std::string_view srcPath, std::string_view folder, int index);

// The cache directory a book at `path` uses, e.g.
// "/.crosspoint/epub_12345678". The prefix follows the book format, matching the
// formulas in Epub.h, Txt.cpp and Xtc.h — a move that guesses the wrong prefix
// orphans the cache and the book silently re-indexes.
// Returns an empty string for a path with no known book extension.
std::string cacheDirFor(std::string_view path);

}  // namespace bookfiling
