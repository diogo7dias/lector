#pragma once

#include <string>

namespace crosspoint {
namespace sleep {

struct SleepPauseToggleResult {
  bool ok = false;       // the move succeeded
  bool toPause = false;  // true = moved into /sleep pause, false = moved back to /sleep
  std::string newPath;   // the file's location after the move (== input path on failure)
};

// If `path` sits directly under /sleep or /sleep pause, move it to the other of the
// two folders (same-volume rename, no RAM copy) and return ok=true. Returns
// ok=false and leaves the file untouched when the path is under neither folder or
// the rename fails. Favorite-path references are repointed on success.
SleepPauseToggleResult toggleSleepPause(const std::string& path);

// True when `path` is directly inside /sleep or /sleep pause (i.e. togglable).
bool isUnderSleepDirs(const std::string& path);

}  // namespace sleep
}  // namespace crosspoint
