#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "LibraryIndexFile.h"

namespace library {

enum class GroupKind : uint8_t { Authors = 0, Series = 1 };

struct GroupEntry {
  std::string name;
  uint16_t count = 0;
};

// Formats a tenths-encoded series index ("#1", "#1.5"); empty string when 0.
inline void formatSeriesBadge(const uint16_t seriesIndexX10, char* out, const size_t size) {
  if (!out || size == 0) return;
  if (seriesIndexX10 == 0) {
    out[0] = '\0';
    return;
  }
  if (seriesIndexX10 % 10 == 0) {
    snprintf(out, size, "#%u", static_cast<unsigned>(seriesIndexX10 / 10));
  } else {
    snprintf(out, size, "#%u.%u", static_cast<unsigned>(seriesIndexX10 / 10),
             static_cast<unsigned>(seriesIndexX10 % 10));
  }
}

// Single sequential pass over the CLX v2 index collecting unique non-empty authors
// or series (case-insensitively grouped, alphabetically sorted).
bool buildGroups(LibraryIndexFile& index, GroupKind kind, std::vector<GroupEntry>& out,
                 ViewProgressFn progress = nullptr, void* progressCtx = nullptr);

// Collects book ordinals belonging to `groupName` (case-insensitive).
// For Series, sorts by seriesIndexX10 ascending (0/unset at end, title tie-break).
// For Authors, keeps title order.
bool collectBooksInGroup(LibraryIndexFile& index, GroupKind kind, std::string_view groupName,
                         std::vector<uint16_t>& out, ViewProgressFn progress = nullptr, void* progressCtx = nullptr);

// Finds the next book in the same series as `currentBookPath` (preferring exact
// seriesIndex + 1, i.e. seriesIndexX10 + 10, else the next higher seriesIndexX10).
// Returns true and sets `nextPathOut` when found.
bool findNextInSeries(LibraryIndexFile& index, std::string_view currentBookPath, std::string& nextPathOut);

}  // namespace library
