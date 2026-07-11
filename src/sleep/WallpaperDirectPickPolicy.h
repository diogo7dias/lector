#pragma once

namespace wallpaper_direct_pick {

enum class Source { SavedOrder, LiveFolder };

inline constexpr Source source(const bool hasSavedCandidate, const bool savedCandidateExists) {
  return hasSavedCandidate && savedCandidateExists ? Source::SavedOrder : Source::LiveFolder;
}

inline constexpr bool shouldMarkFolderDirty(const bool moveSucceeded) { return moveSucceeded; }

}  // namespace wallpaper_direct_pick
