#pragma once

// Which folder the wallpaper rotation reads: /sleep or /.sleep.
//
// Both have always been accepted, and /.sleep used to win outright. That was a
// silent trap. A dotted folder is hidden by macOS Finder and Windows Explorer,
// so a user copying wallpapers onto the card drops them into the visible
// /sleep, while an empty /.sleep left over from an older layout kept winning:
// the index indexed an empty folder, the boot gate's tail probe never moved
// (nothing changed in the folder it was watching), and the sleep screen fell
// back to its default art. No error, no banner, no wallpapers.
//
// The rule is therefore content-based rather than name-based: the folder that
// actually holds something wins, and the visible one wins a tie. Cards written
// by older firmware, which have wallpapers only in /.sleep, keep working
// untouched — no migration, no mass rename.
//
// The device layer answers "has entries" with one cheap directory probe per
// folder (DirSlotProbe's entryExistsAt at slot 0), never a walk.
//
// Host-testable: no SD, no display, no ESP headers.

#include <cstdint>

namespace sleep_folder {

inline constexpr uint8_t kPlainDirId = 0;   // /sleep
inline constexpr uint8_t kHiddenDirId = 1;  // /.sleep

inline uint8_t chooseDirId(const bool plainHasEntries, const bool hiddenHasEntries) {
  if (plainHasEntries) return kPlainDirId;
  if (hiddenHasEntries) return kHiddenDirId;
  // Neither holds anything: anchor on the folder the user can see, so a first
  // fill lands where the empty index already points.
  return kPlainDirId;
}

// Does the folder look different from the last reconcile's markers?
//
// Two independent signals, because neither alone is sound:
//
//   tail  — the slot offset of the folder's last live FAT directory entry.
//           Files appended to a directory push it out, which is what catches
//           images copied onto the card. It is NOT monotone in file count: a
//           FAT delete only marks its slots free in place and the directory
//           never shrinks, so files written later can land in those holes and
//           leave the tail byte-identical.
//   stamp — the folder's own FAT modify date and time, packed into one uint32.
//           Desktop operating systems restamp a directory when its contents
//           change, so it catches exactly the hole-reuse case the tail misses.
//
// A current stamp of 0 means "this driver does not report one". Comparing it
// would force a walk on every single boot, so that case falls back to the tail
// alone. A saved stamp of 0 is the opposite situation: markers written before
// this signal existed. That resolves itself with one walk, which restamps them,
// so it counts as a change rather than silently leaving the signal inert.
inline bool folderMarkersChanged(const uint32_t tailNow, const uint32_t stampNow, const uint32_t tailSaved,
                                 const uint32_t stampSaved) {
  if (tailNow != tailSaved) return true;
  if (stampNow == 0) return false;
  return stampNow != stampSaved;
}

}  // namespace sleep_folder
