#pragma once

#include <string>
#include <string_view>
#include <utility>

#include "util/FavoriteImageNames.h"

// Pure name helpers for the file browser's rows (no Arduino — host testable).
//
// Every row the browser draws has to name a file on the card, and every caller was
// building that path by hand. Both halves of it got it wrong once: the deferred-favorite
// lookup keyed on the on-screen LABEL (extension already stripped, "[F] " already
// prefixed) so it matched no queued job, and the trailing slash the open path leaves on
// basepath turned "/sleep/x.epub" into "/sleep//x.epub", which no cache or queue answers.
// The arithmetic lives here so a test can hold it still.
namespace browser_row {

// The absolute path of a listed entry: the name the card holds, under the browser's
// current folder. `basepath` is trimmed of trailing slashes — the open path appends one
// before opening a book and that state survives the return, so without the trim the key
// becomes "/sleep//name.pxc" and matches nothing.
inline std::string cardPath(std::string_view basepath, std::string_view name) {
  std::string folder(basepath == "/" ? std::string_view() : basepath);
  while (!folder.empty() && folder.back() == '/') folder.pop_back();
  return folder + "/" + std::string(name);
}

// The filename a row must be formatted from, given the name the card holds.
//
// A favorite queued from the image viewer has not been renamed yet, so the listing still
// holds the old name; drawn as-is the row reads as not favorited and the press looks lost.
// `pendingTarget` is called with the card path and answers the name the queue is going to
// give it, or an empty string when nothing is queued.
//
// The lookup and the display formatting are ordered here on purpose. Feeding the label
// back into the lookup is the bug this header exists for, and a caller doing both halves
// itself can reintroduce it; a caller of this cannot.
//
// Only a wallpaper can carry a queued rename, so folders and books never pay for the
// lookup — it takes the queue's mutex and copies the pending jobs, and the glyph prewarm
// pass runs this once per row over a folder that can hold thousands.
template <typename PendingTarget>
std::string rowFile(std::string_view basepath, std::string_view rawName, PendingTarget&& pendingTarget) {
  if (!FavoriteImage::isImageExtension(rawName)) return std::string(rawName);
  const std::string queued = std::forward<PendingTarget>(pendingTarget)(cardPath(basepath, rawName));
  if (queued.empty()) return std::string(rawName);
  const auto slash = queued.find_last_of('/');
  return slash == std::string::npos ? queued : queued.substr(slash + 1);
}

}  // namespace browser_row
