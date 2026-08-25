#pragma once

#include <cstdint>

// The auto-sleep floor for a device that has no manual way to sleep.
//
// Every gesture on every button is rebindable, so Sleep can end up bound to nothing at
// all. Auto-sleep is then the only thing that puts the device down, and a timeout of
// thirty minutes (or Never) leaves it awake in a bag until the battery is gone. While no
// Sleep binding exists anywhere, the timeout the loop reads is capped.
//
// The cap applies to the effective value only. The stored setting is never rewritten, so
// binding Sleep to any gesture on any button restores the user's own timeout with nothing
// to undo by hand.
namespace sleep_guard {

// Long enough to put the reader down mid-page and pick it back up, short enough that a
// device with no sleep binding cannot idle awake for long.
constexpr uint8_t NO_SLEEP_BINDING_MAX_MINUTES = 3;

// `neverValue` is the stored value that means "never sleep"; it is capped like any other,
// because "never" is exactly the case this guards against.
inline uint8_t effectiveMinutes(const uint8_t storedMinutes, const bool sleepBoundSomewhere,
                                const uint8_t neverValue) {
  if (sleepBoundSomewhere) return storedMinutes;
  if (storedMinutes >= neverValue) return NO_SLEEP_BINDING_MAX_MINUTES;
  return storedMinutes < NO_SLEEP_BINDING_MAX_MINUTES ? storedMinutes : NO_SLEEP_BINDING_MAX_MINUTES;
}

}  // namespace sleep_guard
