#pragma once

#include <cstdint>

// The reader's four margins and the one rule that binds them.
//
// Left/right are always a single stored value (horizontal); top and bottom are ALWAYS two
// stored values, so every reader can use them directly without knowing the mode. The mode
// is only a statement about editing:
//
//   Separate   left/right one value, top and bottom edited on their own
//   TopBottom  editing either vertical side writes both; the list shows one Vertical row
//   AllSides   every side is the horizontal value; the list shows a single Margin row
//
// Keeping the rule here (rather than inline in a settings screen) is what lets it be
// tested without a panel, and what stops the two editors, Reader Settings and the global
// settings list, plus the migration, from drifting apart.
namespace margin_link {

enum class Mode : uint8_t {
  Separate = 0,
  TopBottom = 1,
  AllSides = 2,
};

inline constexpr Mode DEFAULT_MODE = Mode::AllSides;
inline constexpr uint8_t MODE_COUNT = 3;

struct Margins {
  uint8_t horizontal = 0;
  uint8_t top = 0;
  uint8_t bottom = 0;

  bool operator==(const Margins&) const = default;
};

// The whole editable state the rule touches. Dynamic Margins is in here because it drives
// the horizontal margin on its own, which All Sides cannot allow.
struct State {
  Margins margins;
  uint8_t dynamicMargins = 0;
  Mode mode = DEFAULT_MODE;

  bool operator==(const State&) const = default;
};

constexpr Mode toMode(const uint8_t stored) {
  return stored <= static_cast<uint8_t>(Mode::AllSides) ? static_cast<Mode>(stored) : DEFAULT_MODE;
}

constexpr uint8_t toStored(const Mode mode) { return static_cast<uint8_t>(mode); }

constexpr Mode nextMode(const Mode mode) { return toMode(static_cast<uint8_t>((toStored(mode) + 1) % MODE_COUNT)); }

// Editing a vertical side: linked modes write both, Separate leaves the other side alone.
constexpr Margins setTop(const Margins current, const uint8_t value, const Mode mode) {
  return mode == Mode::Separate ? Margins{current.horizontal, value, current.bottom}
                                : Margins{current.horizontal, value, value};
}

constexpr Margins setBottom(const Margins current, const uint8_t value, const Mode mode) {
  return mode == Mode::Separate ? Margins{current.horizontal, current.top, value}
                                : Margins{current.horizontal, value, value};
}

// Editing the horizontal margin. In All Sides that row IS every side, so it carries the
// vertical values too.
constexpr Margins setHorizontal(const Margins current, const uint8_t value, const Mode mode) {
  return mode == Mode::AllSides ? Margins{value, value, value} : Margins{value, current.top, current.bottom};
}

// Switching mode. Linking the vertical sides adopts the top value, and All Sides adopts
// the horizontal one, so the page never keeps a margin the list no longer shows a row for.
// All Sides also forces Dynamic Margins off: it computes its own horizontal margin, which
// would leave left/right disagreeing with top/bottom. Leaving All Sides does not bring it
// back — the stored value always says what is true.
constexpr State applyMode(const State current, const Mode mode) {
  switch (mode) {
    case Mode::AllSides:
      return {{current.margins.horizontal, current.margins.horizontal, current.margins.horizontal}, 0, mode};
    case Mode::TopBottom:
      return {{current.margins.horizontal, current.margins.top, current.margins.top}, current.dynamicMargins, mode};
    case Mode::Separate:
      break;
  }
  return {current.margins, current.dynamicMargins, mode};
}

// Settings files written before the Horizontal/Vertical split carry "uniformMargins".
// On, it meant every side used the horizontal margin, so the file holds no vertical values
// of its own — which is exactly All Sides. Off, the two stored values were already the
// vertical margins, edited on their own.
constexpr State migrateFromUniform(const bool uniform, const Margins stored, const uint8_t dynamicMargins) {
  return uniform ? applyMode({stored, dynamicMargins, Mode::AllSides}, Mode::AllSides)
                 : State{stored, dynamicMargins, Mode::Separate};
}

}  // namespace margin_link
