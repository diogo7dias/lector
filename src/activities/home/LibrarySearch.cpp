#include "activities/home/LibrarySearch.h"

#include <algorithm>
#include <cctype>

namespace librarysearch {
namespace {

char lower(const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

// A word starts after anything that is not a letter or digit, which covers spaces,
// punctuation and the underscores and dashes filenames are full of.
bool isWordBoundary(const char c) { return std::isalnum(static_cast<unsigned char>(c)) == 0; }

bool startsWith(const std::string_view name, const std::string_view query, const size_t at) {
  if (query.size() > name.size() - at) return false;
  for (size_t i = 0; i < query.size(); i++) {
    if (lower(name[at + i]) != query[i]) return false;
  }
  return true;
}

}  // namespace

bool scoreEntry(std::string_view name, const std::string_view query, Match& out) {
  if (query.empty() || name.empty()) return false;
  if (name.back() == '/') name.remove_suffix(1);  // directory entries carry a trailing slash
  if (name.empty()) return false;

  std::string needle;
  needle.reserve(query.size());
  for (const char c : query) needle += lower(c);

  if (startsWith(name, needle, 0)) {
    out = Match{0, 0};
    return true;
  }

  // Tier 1: the earliest word that starts with the query. Score is where that word
  // begins, so "Gatsby" beats a hit twenty characters later.
  for (size_t i = 1; i < name.size(); i++) {
    if (!isWordBoundary(name[i - 1])) continue;
    if (startsWith(name, needle, i)) {
      out = Match{1, static_cast<int>(i)};
      return true;
    }
  }

  // Tier 2: letters in order with gaps. Score is where the run starts plus how far it
  // spreads, so a tight early match ranks above a scattered one.
  size_t q = 0;
  size_t first = std::string_view::npos;
  size_t last = 0;
  for (size_t i = 0; i < name.size() && q < needle.size(); i++) {
    if (lower(name[i]) != needle[q]) continue;
    if (first == std::string_view::npos) first = i;
    last = i;
    q++;
  }
  if (q < needle.size()) return false;

  out = Match{2, static_cast<int>(first + (last - first))};
  return true;
}

std::vector<int> rankMatches(const std::vector<std::string>& names, const std::string_view query) {
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
