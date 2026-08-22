#pragma once

#include <cstdint>

// The reader's two vertical margins and the one rule that binds them.
//
// Top and bottom are ALWAYS two stored values, so every reader can use them directly.
// "Linked" is only a statement about editing: while it is on, the two are kept equal, and
// the settings screen shows a single Vertical Margin row instead of two. Keeping the rule
// here (rather than inline in the settings screen) is what lets it be tested without a
// panel, and what stops the migration and the editor from drifting apart.
namespace vertical_margins {

struct Margins {
  uint8_t top = 0;
  uint8_t bottom = 0;

  bool operator==(const Margins&) const = default;
};

// Editing the single linked row: both sides take the value. Editing while unlinked leaves
// the other side alone.
constexpr Margins setTop(const Margins current, const uint8_t value, const bool linked) {
  return linked ? Margins{value, value} : Margins{value, current.bottom};
}

constexpr Margins setBottom(const Margins current, const uint8_t value, const bool linked) {
  return linked ? Margins{value, value} : Margins{current.top, value};
}

// Turning the link back on adopts the top value, so the page never keeps a bottom margin
// the list no longer shows a row for.
constexpr Margins relink(const Margins current) { return {current.top, current.top}; }

// Settings files written before the Horizontal/Vertical split carry "uniformMargins".
// On, it meant every side used the horizontal margin, so the file holds no vertical values
// of its own and both take the horizontal one — which is what the reader was already
// drawing. Off, the two stored values were already the vertical margins.
constexpr Margins migrateFromUniform(const bool uniform, const uint8_t horizontal, const Margins stored) {
  return uniform ? Margins{horizontal, horizontal} : stored;
}

}  // namespace vertical_margins
