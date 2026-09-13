#pragma once

// Pure policy: where does a wake land?
//
// Math core only: no SD, no framebuffer, no settings object, so it is host-testable.

#include <cstdint>

namespace wake_route {

enum class Route : uint8_t {
  Unchanged,    // fall through to the ordinary boot routing
  ForceReader,  // open the book, whatever the ordinary routing would have done
  ForceHome,    // land on home, whatever the ordinary routing would have done
};

struct WakeInputs {
  // The Light sleep face named a book on the sleep screen and must open it.
  bool forceBookOnWake = false;
  // There is a book path to open.
  bool hasBook = false;
  // The ordinary routing's own inputs, kept here so the tests state them explicitly.
  bool sleptFromReader = false;
  bool backHeld = false;
  bool bookOnBoot = false;
  // The reader failed to come up last boot (readerActivityLoadCount > 0).
  bool readerCrashed = false;
};

inline constexpr Route resolve(const WakeInputs& in) {
  if (!in.forceBookOnWake) return Route::Unchanged;
  // Safety valves first: they outrank both promises.
  if (in.readerCrashed || !in.hasBook) return Route::ForceHome;
  return Route::ForceReader;
}

}  // namespace wake_route
