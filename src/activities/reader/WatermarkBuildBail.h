#pragma once

// Yield a blocking watermark-extension loop when the requested page still does
// not exist. loop() then drives the build in BACKGROUND_BUILD_PAGES_PER_TICK
// slices so input is not frozen; Section::suspendBuild() on exit keeps the
// pages already laid out (the same path the background builder already uses).
inline bool watermarkBuildShouldYield(const int currentPage, const int pageCount, const bool buildComplete) {
  if (buildComplete) return false;
  return currentPage >= pageCount;
}

// True when this background tick laid out the page render() was waiting on.
inline bool waitingPageBecameReadable(const int currentPage, const int pageCountBefore, const int pageCountAfter) {
  return currentPage >= pageCountBefore && currentPage < pageCountAfter;
}
