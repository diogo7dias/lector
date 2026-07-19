#pragma once

#include <string>

// Sleep-wallpaper reference bookkeeping. When a wallpaper file under /sleep or
// /sleep pause is renamed, moved, or deleted, the rotation cursor references in
// APP_STATE (lastShownSleepFilename basename + lastSleepWallpaperPath) must be
// repointed or cleared, or the rotation is left pointing at a dead name. These
// helpers are the single home for that fixup; they are called by the pause/move
// and delete triage paths.
namespace crosspoint {
namespace sleep {

// Fix up APP_STATE sleep references when a wallpaper file is renamed/moved from
// `oldPath` to `newPath`.
void replacePathReferences(const std::string& oldPath, const std::string& newPath);

// Clear APP_STATE sleep references when a wallpaper file is deleted.
void removePathReferences(const std::string& path);

}  // namespace sleep
}  // namespace crosspoint
