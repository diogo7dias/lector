#pragma once

#include <vector>

// Cursor movement for the flat settings list (no hardware deps — host testable).
//
// The list is one long run of rows with section headings spliced in. Headings are
// drawn but never landable, so a step has to walk past them.
// Callers pass the headings as a flag per row, which keeps this free of SettingInfo
// and testable on its own.
namespace settings_nav {

inline int firstLandableRow(const std::vector<bool>& isHeader) {
  for (int i = 0; i < static_cast<int>(isHeader.size()); ++i) {
    if (!isHeader[i]) return i;
  }
  return 0;
}

// One step up or down, wrapping around the list and skipping headings. Returns the
// index unchanged when no row is landable, so a caller can never spin here.
inline int nextRow(const int index, const std::vector<bool>& isHeader, const bool forward) {
  const int count = static_cast<int>(isHeader.size());
  if (count == 0) return index;
  int i = index;
  for (int guard = 0; guard < count; ++guard) {
    i = forward ? (i + 1) % count : (i - 1 + count) % count;
    if (!isHeader[i]) return i;
  }
  return index;
}

}  // namespace settings_nav
