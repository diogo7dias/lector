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

#include <algorithm>
#include <cstddef>
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
// Templated on the container so it serves both a std::vector<std::string> and the
// arena-backed NameList the file browser uses; it needs only size() and an
// operator[] whose result converts to std::string_view.
template <typename Names>
std::vector<int> rankMatches(const Names& names, const std::string_view query) {
  std::vector<int> hits;
  if (query.empty()) return hits;

  // Score and index travel together, or sorting one would lose track of the other.
  struct Scored {
    int index;
    Match match;
  };
  std::vector<Scored> scored;
  scored.reserve(names.size());
  for (size_t i = 0; i < names.size(); i++) {
    Match m;
    if (!scoreEntry(names[i], query, m)) continue;
    scored.push_back(Scored{static_cast<int>(i), m});
  }

  // stable_sort so equally good matches keep the listing order the folder already had,
  // rather than being reshuffled by the sort.
  std::stable_sort(scored.begin(), scored.end(), [](const Scored& a, const Scored& b) {
    if (a.match.tier != b.match.tier) return a.match.tier < b.match.tier;
    return a.match.score < b.match.score;
  });

  hits.reserve(scored.size());
  for (const Scored& s : scored) hits.push_back(s.index);
  return hits;
}

}  // namespace librarysearch
