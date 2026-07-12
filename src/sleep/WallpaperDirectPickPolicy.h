#pragma once

namespace wallpaper_direct_pick {

enum class Source { SavedOrder, LiveFolder };

inline constexpr Source source(const bool hasSavedCandidate, const bool savedCandidateExists) {
  return hasSavedCandidate && savedCandidateExists ? Source::SavedOrder : Source::LiveFolder;
}

inline constexpr bool shouldMarkFolderDirty(const bool moveSucceeded) { return moveSucceeded; }

// True when a chosen wallpaper must be skipped because it would repeat the one
// shown last time. The buffer-backed and direct-pick engines keep separate
// cursors, so a low-memory switch between them (or a reset direct cursor) can
// re-pick the just-shown file even though each engine avoids repeats internally.
// A repeat is only rejected when rotation is active (not paused) and more than
// one wallpaper exists — with a single file, or while rotation is paused,
// re-showing the same image is correct.
inline constexpr bool isImmediateRepeat(const bool rotationPaused, const bool sameAsLastShown,
                                        const bool moreThanOneFile) {
  return !rotationPaused && sameAsLastShown && moreThanOneFile;
}

}  // namespace wallpaper_direct_pick
