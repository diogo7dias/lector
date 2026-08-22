#pragma once

#include <vector>

// Cursor movement for the flat settings list (no hardware deps — host testable).
//
// The list is one long run of rows with section headings spliced in. Headings are
// drawn but never landable, so both step and section jumps have to walk past them.
// Callers pass the headings as a flag per row, which keeps this free of SettingInfo
// and testable on its own.
namespace settings_nav {

// The first landable row of every section, in list order. A run of landable rows
// before the first heading counts as a section too, so a list that somehow starts
// without a heading still has somewhere to jump to.
inline std::vector<int> sectionStarts(const std::vector<bool>& isHeader) {
  std::vector<int> starts;
  const int count = static_cast<int>(isHeader.size());
  if (count > 0 && !isHeader[0]) starts.push_back(0);
  for (int i = 0; i < count; ++i) {
    // A heading with no row under it (list ends on one, or two headings in a row)
    // opens no section, so it is not a jump target.
    if (isHeader[i] || i == 0) continue;
    if (isHeader[i - 1]) starts.push_back(i);
  }
  return starts;
}

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

// Jump a whole section, landing on its first row rather than on its heading.
//
// Backward is sticky: from the middle of a section the first press goes to that
// section's own first row, and only a press from there moves to the section above.
// Without it, entering a section and pressing back would overshoot it entirely.
inline int nextSection(const int index, const std::vector<bool>& isHeader, const bool forward) {
  const std::vector<int> starts = sectionStarts(isHeader);
  if (starts.empty()) return index;

  if (forward) {
    for (const int start : starts) {
      if (start > index) return start;
    }
    return starts.front();
  }

  int candidate = -1;
  for (const int start : starts) {
    if (start < index) candidate = start;
  }
  if (candidate >= 0) return candidate;
  // Already at or above the first section start: wrap to the last section.
  return starts.back();
}

}  // namespace settings_nav
