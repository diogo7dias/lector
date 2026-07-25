#pragma once

// Ranking for the file browser's in-folder search.
// No Arduino types, no Storage — pure, and host-tested (test/library_search).
//
// Plain substring matching is not enough on a card full of books: the user
// remembers a word from the middle of a title, or types initials. So a name can
// match three ways, best first:
//   tier 0  the name starts with the query
//   tier 1  a word inside the name starts with the query
//   tier 2  the query's letters appear in order, with gaps ("gtg" -> "Great Gatsby")
// Within a tier the tighter, earlier match wins; equal matches keep listing order,
// so a search never reshuffles equally good results on its own.

#include <string>
#include <string_view>
#include <vector>

namespace librarysearch {

struct Match {
  int tier = 0;   // 0 best
  int score = 0;  // lower is better within a tier
};

// Scores one name. `query` is matched case-insensitively (ASCII); a trailing '/'
// on a directory entry is ignored. Returns false when the name does not match at
// all, leaving `out` untouched. An empty query matches nothing — the caller shows
// the unfiltered list instead.
bool scoreEntry(std::string_view name, std::string_view query, Match& out);

// Indices into `names` of every entry that matches, best first.
std::vector<int> rankMatches(const std::vector<std::string>& names, std::string_view query);

}  // namespace librarysearch
