#pragma once

// Pure policy: where does a wake land?
//
// Two sleep faces make a promise about the wake that the ordinary routing in
// setup() cannot keep on its own:
//
//   Quick Resume promises nothing changes. The panel already holds the screen you
//   locked from, so the wake must rebuild that same screen and nothing else. In
//   particular "Open Book on Boot" must not fire — it would name one book
//   on the sleep screen and open another on the wake — and a held Back must not
//   divert to home, because Back is not an escape hatch on a face that never left
//   the screen in the first place.
//
//   The Light face promises the book it names. It draws the title of the book the
//   wake is about to open, so the wake opens it even when the lock happened on the
//   home menu, where the ordinary routing would go home.
//
// Two safety valves outrank both promises: a book that crashed the reader last boot
// (readerActivityLoadCount > 0) and a wake with no book path at all. Either one lands
// on home, so a book that cannot open can never wedge the device somewhere with no
// way back to the library.
//
// Every other sleep face returns Unchanged and keeps the routing it has today.
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
  // Woke from a Quick Resume frame (the pre-sleep framebuffer was restored).
  bool quickResume = false;
  // Which screen that frame shows. The lock repaints home before stamping the moon
  // when the screen it locked from cannot be rebuilt, so this is always a screen the
  // wake can actually reach.
  bool quickResumeTargetIsReader = false;
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
  if (!in.quickResume && !in.forceBookOnWake) return Route::Unchanged;
  // Safety valves first: they outrank both promises.
  if (in.readerCrashed || !in.hasBook) return Route::ForceHome;
  if (in.quickResume) return in.quickResumeTargetIsReader ? Route::ForceReader : Route::ForceHome;
  return Route::ForceReader;
}

// Lock side, Quick Resume only. Deep sleep is a chip reset, so only the reader page
// can be rebuilt on the wake; a settings screen or a file browser cannot. Locking
// from one of those repaints home BEFORE the moon is stamped and the frame saved, so
// the sleeping screen and the woken screen are the same picture.
inline constexpr bool quickResumeNeedsHomeRepaint(const bool targetIsReader, const bool alreadyOnHome) {
  return !targetIsReader && !alreadyOnHome;
}

}  // namespace wake_route
