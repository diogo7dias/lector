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

// True when a reopened partial already holds the landing page: show it now and leave the
// extension build to loop(), instead of starting it (and its popup) in front of the page.
inline bool partialServesPage(const int targetPage, const int partialPageCount) {
  return targetPage < partialPageCount;
}

// Whether loop() lays out more of the current chapter this tick. While the reader turns pages
// the build keeps only `windowAhead` pages past them, so a tick never sits between a turn and
// its render. Once the reader has been idle past the debounce it builds on to the chapter's
// end: a chapter left after a few pages would otherwise stay partial and be laid out again
// from page 0 every session. An extension of a partial always runs, since its pages past the
// saved watermark only count once it finishes.
inline bool backgroundBuildTickWanted(const bool partial, const int pageCount, const int currentPage,
                                      const int windowAhead, const bool readerIdle) {
  return partial || readerIdle || pageCount < currentPage + windowAhead;
}
