#pragma once

namespace image_viewer_policy {

constexpr bool loadSiblingsOnEnter(const bool resultMode, const bool inSleepDirectory) {
  return !resultMode || !inSleepDirectory;
}

constexpr bool showUnloadedNavigationHints(const bool resultMode, const bool inSleepDirectory,
                                           const bool siblingsLoaded) {
  return !siblingsLoaded && !loadSiblingsOnEnter(resultMode, inSleepDirectory);
}

constexpr bool refreshHintsAfterNavigation(const bool siblingsLoadedBeforeAction, const bool navigationMoved) {
  return !siblingsLoadedBeforeAction && !navigationMoved;
}

}  // namespace image_viewer_policy
