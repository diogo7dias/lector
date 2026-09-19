#pragma once
#include <string>
#include <vector>
struct RecentBook {
  std::string path;
  int progressPercent = -1;
};
struct RecentBooksStore {
  static bool isMissing(const RecentBook&) { return false; }
  const std::vector<RecentBook>& getBooks() const {
    static const std::vector<RecentBook> empty;
    return empty;
  }
};
inline RecentBooksStore recentBooksStub;
#define RECENT_BOOKS recentBooksStub
