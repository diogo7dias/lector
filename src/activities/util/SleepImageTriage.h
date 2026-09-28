#pragma once

#include <string>

class GfxRenderer;

// Sleep-folder triage shared by BmpViewerActivity and PxcViewerActivity, so the two
// image viewers cannot drift apart again. Every call that can fail reports the failure
// with a popup itself and returns false (or an empty path); the caller only re-renders.
namespace SleepImageTriage {

// The one line an error state gets, in the theme's own help face: these screens
// are an image and nothing else, so the message is all the chrome they have.
int lineHeightForHelp(const GfxRenderer& renderer);

// The path this file WILL have once the favorite queue drains, or `path` itself when
// nothing is queued for it. Everything the user is shown reads through here: the press
// that favorites a wallpaper only queues the rename, so the card still holds the old
// name for a while, and a hint strip reading the card alone would say "Favorite" again
// on a file the user has just favorited.
std::string effectivePath(const std::string& path);
bool effectiveFavorite(const std::string& path);

// Queues the favorite rename rather than doing it on the press: on a FAT card a rename
// is a linear scan of the directory, and a wallpaper folder holds thousands of files, so
// doing it here pinned the viewer for seconds. DeferredFavorite drains the queue when the
// browser is left or the device is locked. Falls back to the foreground rename when the
// queue is jammed.
bool toggleFavorite(GfxRenderer& renderer, const std::string& path);

// A queued favorite rename and a delete or pause move are all name-based operations on
// one file: drain the queue first and return the post-drain name. Draining is what
// performs the rename, so the name captured before it is exactly the one that stops
// existing.
std::string drainFavorites(const std::string& path);

// Drains, then deletes the file and every reference to it. `path` is updated to the
// post-drain name whether or not the delete succeeds.
bool deleteImage(GfxRenderer& renderer, std::string& path);

// Moves the file between /sleep and "/sleep pause". Call drainFavorites() first.
bool togglePause(GfxRenderer& renderer, const std::string& path);

}  // namespace SleepImageTriage
