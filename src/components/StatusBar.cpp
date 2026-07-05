#include "StatusBar.h"

int statusClusterWidth(const std::vector<StatusSegment>& segs, int sepWidth) {
  if (segs.empty()) return 0;
  int w = 0;
  for (const auto& s : segs) w += s.width;
  w += sepWidth * (static_cast<int>(segs.size()) - 1);
  return w;
}

namespace {

int clusterWidth(const std::vector<StatusSegment>& segs, int sepWidth) { return statusClusterWidth(segs, sepWidth); }

bool clusterHasGreedy(const std::vector<StatusSegment>& segs) {
  for (const auto& s : segs) {
    if (s.greedy) return true;
  }
  return false;
}

// Fall-down target order for a bumped top-band anchor (indices into anchor[]):
// TL(0) -> BL,BC,BR ; TC(1) -> BC,BL,BR ("down then inward") ; TR(2) -> BR,BC,BL.
// Bottom anchors have nowhere to fall.
const std::vector<int>& fallChain(int idx) {
  static const std::vector<int> tl = {3, 4, 5};
  static const std::vector<int> tc = {4, 3, 5};
  static const std::vector<int> tr = {5, 4, 3};
  static const std::vector<int> none = {};
  switch (idx) {
    case 0:
      return tl;
    case 1:
      return tc;
    case 2:
      return tr;
    default:
      return none;
  }
}

}  // namespace

ResolvedStatusBar composeStatusBar(std::array<std::vector<StatusSegment>, 6> a, int bandWidth, int sepWidth) {
  // Append a bumped segment to the first fall-chain target whose (bottom) band
  // still fits it; returns false if it fits nowhere and is therefore dropped.
  auto tryPlace = [&](int fromIdx, const StatusSegment& seg) -> bool {
    for (int target : fallChain(fromIdx)) {
      const int addW = a[target].empty() ? seg.width : sepWidth + seg.width;
      const int bl = clusterWidth(a[3], sepWidth) + (target == 3 ? addW : 0);
      const int bc = clusterWidth(a[4], sepWidth) + (target == 4 ? addW : 0);
      const int br = clusterWidth(a[5], sepWidth) + (target == 5 ? addW : 0);
      if (bl + bc + br <= bandWidth) {
        a[target].push_back(seg);
        return true;
      }
    }
    return false;
  };

  // Resolve a band's overflow by bumping non-greedy segments to make room for a
  // greedy title. Only runs when the band actually holds a greedy segment — a
  // band of fixed-width items is left as-is (the renderer truncates if needed).
  // L/C/R are the anchor indices; canFall = bumped items cascade to the far band.
  auto resolveBand = [&](int L, int C, int R, bool canFall) {
    if (!clusterHasGreedy(a[L]) && !clusterHasGreedy(a[C]) && !clusterHasGreedy(a[R])) return;
    for (int guard = 0; guard < 16; ++guard) {
      if (clusterWidth(a[L], sepWidth) + clusterWidth(a[C], sepWidth) + clusterWidth(a[R], sepWidth) <= bandWidth) {
        return;  // fits
      }
      // Pick a segment to bump: prefer the right corner, then left, then an extra
      // (non-sole) centre segment. Never bump a greedy segment.
      int victim = -1;
      if (!a[R].empty() && !a[R].back().greedy) {
        victim = R;
      } else if (!a[L].empty() && !a[L].back().greedy) {
        victim = L;
      } else if (a[C].size() > 1 && !a[C].back().greedy) {
        victim = C;
      }
      if (victim == -1) return;  // only greedy content remains; draw-time truncation handles it
      StatusSegment seg = a[victim].back();
      a[victim].pop_back();
      if (canFall) tryPlace(victim, seg);  // else the segment is dropped (hidden)
    }
  };

  // Resolve the bottom band's own overflow first (bumped bottom items just hide),
  // so the top band's fall-through sees a settled bottom budget.
  resolveBand(3, 4, 5, /*canFall=*/false);
  // Then the top band, whose bumped items cascade down into the bottom band.
  resolveBand(0, 1, 2, /*canFall=*/true);

  ResolvedStatusBar out;
  out.anchor = std::move(a);
  out.hasTopBand = !out.anchor[0].empty() || !out.anchor[1].empty() || !out.anchor[2].empty();
  out.hasBottomBand = !out.anchor[3].empty() || !out.anchor[4].empty() || !out.anchor[5].empty();
  return out;
}
