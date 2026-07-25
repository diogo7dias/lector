#pragma once

#include <string>

namespace crosspoint {
namespace sleep {

// "/sleep pause" is an ordinary folder that the sleep screen simply does not read
// from. Moving a wallpaper into it takes that image out of rotation without
// deleting it; moving it back puts it in again. There is no separate paused
// flag to keep in sync — the file's location IS the state, exactly like the _F
// favorite suffix.
constexpr const char* kSleepDir = "/sleep";
constexpr const char* kSleepPauseDir = "/sleep pause";

struct SleepPauseToggleResult {
  bool ok = false;       // the move succeeded
  bool toPause = false;  // true = moved into "/sleep pause", false = moved back to /sleep
  std::string newPath;   // the file's location after the move (== input path on failure)
};

// If `path` sits directly under /sleep or "/sleep pause", move it to the other of
// the two folders (same-volume rename, no RAM copy) and return ok=true. Returns
// ok=false and leaves the file untouched when the path is under neither folder or
// the rename fails. The APP_STATE wallpaper reference is repointed on success.
SleepPauseToggleResult toggleSleepPause(const std::string& path);

// True when `path` is directly inside /sleep or "/sleep pause" (i.e. togglable).
bool isUnderSleepDirs(const std::string& path);

}  // namespace sleep
}  // namespace crosspoint
