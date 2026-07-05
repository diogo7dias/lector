#pragma once
#include <cstdint>
#include <string>

// ---------------------------------------------------------------------------
// Status bar model (v2): per-item, six-anchor layout.
//
// The renderer (BaseTheme::drawStatusBarV2) builds the bar entirely on the stack
// with fixed char buffers — NO std::vector / std::string / composer in the render
// path. The reader status bar draws on a lock-holding, stack-constrained render
// task, and an earlier heap/vector-based design overflowed that stack and bricked
// boot, so this stays deliberately allocation-free.
// ---------------------------------------------------------------------------

// Data the active reader supplies. Battery and clock are pulled from the HAL by the
// renderer, not provided here. Chapter-based fields are only meaningful when
// hasChapters is true (EPUB, chaptered XTC); otherwise chapter items are hidden.
struct StatusBarData {
  bool hasChapters = false;
  int chapterPage = 0;     // 1-based page within the chapter
  int chapterPages = 0;    // total pages in the chapter
  int chapterNum = 0;      // 1-based chapter index
  int chapterTotal = 0;    // total chapters
  int bookPercent = 0;     // 0..100
  int chapterPercent = 0;  // 0..100
  std::string bookTitle;
  std::string chapterTitle;
  bool bookmarked = false;
};

// Progress bar thickness in pixels for the slim/medium/fat setting (0/1/2). Kept
// deliberately far apart so the three levels read as "plenty distinct".
inline int statusBarThicknessPx(uint8_t thickness) {
  switch (thickness) {
    case 0:
      return 2;  // slim
    case 2:
      return 12;  // fat
    default:
      return 6;  // medium
  }
}
