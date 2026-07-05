#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Status bar model (v2): per-item, six-anchor layout with reflow.
//
// The renderer maps settings + the active reader's data into a set of "segments"
// bucketed by anchor, then composeStatusBar() resolves overlap: a greedy
// (truncate-off) title keeps its space and bumps neighbours, which fall straight
// down to the opposite band. The compose step is pure (no rendering, no settings,
// no HAL) so it can be unit-tested on the host.
// ---------------------------------------------------------------------------

// Data the active reader supplies. Battery and clock are pulled from the HAL by
// the renderer, not provided here. Chapter-based fields are only meaningful when
// hasChapters is true (EPUB, chaptered XTC); otherwise they are ignored.
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

// One drawable piece of an anchor cluster.
struct StatusSegment {
  enum Type : uint8_t { TEXT, BATTERY };
  Type type = TEXT;
  std::string text;     // TEXT: the string. BATTERY: the "NN%" text (may be empty).
  int width = 0;        // pre-measured pixel width (BATTERY includes icon + gap + %)
  bool greedy = false;  // truncate-off title: keeps its space, bumps neighbours
};

// Anchor index convention: 0=TL 1=TC 2=TR 3=BL 4=BC 5=BR (= SB_ANCHOR_* value - 1).
namespace StatusBarAnchor {
inline int index(uint8_t anchorEnum) { return static_cast<int>(anchorEnum) - 1; }  // SB_ANCHOR_TL(1) -> 0
inline bool isTop(int idx) { return idx >= 0 && idx < 3; }
inline bool isLeft(int idx) { return idx % 3 == 0; }
inline bool isCenter(int idx) { return idx % 3 == 1; }
inline bool isRight(int idx) { return idx % 3 == 2; }
}  // namespace StatusBarAnchor

// Result of composition: final segment lists per anchor (same index convention).
struct ResolvedStatusBar {
  std::array<std::vector<StatusSegment>, 6> anchor;
  bool hasTopBand = false;
  bool hasBottomBand = false;
};

// Resolve overlap/reflow. bandWidth = usable horizontal space; sepWidth = pixel
// width of the " | " separator drawn between co-anchored segments. Bumping is
// triggered only by a greedy title; without one, an over-wide band is left for the
// renderer to truncate at draw time.
ResolvedStatusBar composeStatusBar(std::array<std::vector<StatusSegment>, 6> initial, int bandWidth, int sepWidth);

// Total pixel width of a cluster: sum of segment widths plus a separator between
// each pair.
int statusClusterWidth(const std::vector<StatusSegment>& segs, int sepWidth);

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
