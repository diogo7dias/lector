#pragma once

namespace image_viewer_policy {

constexpr bool loadSiblingsOnEnter(const bool resultMode, const bool inSleepDirectory) {
  return !resultMode || !inSleepDirectory;
}

constexpr bool showUnloadedNavigationHints(const bool resultMode, const bool inSleepDirectory,
                                           const bool siblingsLoaded) {
  return !siblingsLoaded && !loadSiblingsOnEnter(resultMode, inSleepDirectory);
}

}  // namespace image_viewer_policy
