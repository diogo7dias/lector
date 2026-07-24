#include "StatusBar.h"

// Pure reflow logic for the v2 status bar. NO Arduino / GfxRenderer / heap here
// (see StatusBar.h) so it can run on the render task and be host-tested.

namespace statusbar {

int clusterWidth(const BarLayout& layout, int anchor, int sepW) {
  const int n = layout.counts[anchor];
  if (n <= 0) return 0;
  int w = 0;
  for (int i = 0; i < n; i++) w += layout.buckets[anchor][i].width;
  w += sepW * (n - 1);
  return w;
}

namespace {

// Horizontal extent [start, end] a cluster occupies inside its band, given its
// column alignment (0 left-edge, 1 centred, 2 right-edge).
void clusterExtent(const BarLayout& layout, int anchor, int bandWidth, int sepW, int* start, int* end) {
  const int w = clusterWidth(layout, anchor, sepW);
  const int col = anchor % 3;
  int s;
  if (col == 0)
    s = 0;
  else if (col == 2)
    s = bandWidth - w;
  else
    s = (bandWidth - w) / 2;
  *start = s;
  *end = s + w;
}

// Move every live segment from `src` onto the end of `dst` (append, preserving
// enable-order), then empty `src`. Caller guarantees the combined count fits.
void moveCluster(BarLayout& layout, int src, int dst) {
  int d = layout.counts[dst];
  const int n = layout.counts[src];
  for (int i = 0; i < n && d < kMaxPerAnchor; i++) layout.buckets[dst][d++] = layout.buckets[src][i];
  layout.counts[dst] = d;
  layout.counts[src] = 0;
}

// Relocate a bumped cluster to the opposite band. Searches same-side-first; sits
// at the first free anchor, else joins the first occupied anchor that still
// leaves the band's other two columns room, else hides (drops) the cluster.
void bumpCluster(BarLayout& layout, int src, bool destBandReserved, int bandWidth, int sepW) {
  const int scol = src % 3;
  const int dband = 1 - (src / 3);

  // If the opposite band has no reserved height, there is nowhere legal to draw
  // a bumped item — hide it rather than paint into unreserved space.
  if (!destBandReserved) {
    layout.counts[src] = 0;
    return;
  }

  // Column search order within the opposite band, nearest-side first.
  int order[3];
  if (scol == 0) {
    order[0] = 0;
    order[1] = 1;
    order[2] = 2;
  } else if (scol == 2) {
    order[0] = 2;
    order[1] = 1;
    order[2] = 0;
  } else {
    order[0] = 1;
    order[1] = 0;
    order[2] = 2;
  }

  const int movedW = clusterWidth(layout, src, sepW);
  for (int k = 0; k < 3; k++) {
    const int dst = dband * 3 + order[k];
    if (layout.counts[dst] == 0) {
      moveCluster(layout, src, dst);  // lands empty -> sit
      return;
    }
    // Occupied: join only if the merged cluster plus the band's other two
    // columns still fit, and the bucket has room for the extra segments.
    if (layout.counts[dst] + layout.counts[src] > kMaxPerAnchor) continue;
    const int existingW = clusterWidth(layout, dst, sepW);
    const int combined = existingW + sepW + movedW;
    int otherW = 0;
    for (int c = 0; c < 3; c++) {
      if (c == order[k]) continue;
      otherW += clusterWidth(layout, dband * 3 + c, sepW);
    }
    if (combined + otherW <= bandWidth) {
      moveCluster(layout, src, dst);  // roomy -> join
      return;
    }
  }
  layout.counts[src] = 0;  // fits nowhere -> hidden
}

}  // namespace

void reflowTitle(BarLayout& layout, int titleAnchor, bool titleTruncate, int bandWidth, int sepW,
                 bool destBandReserved) {
  if (titleAnchor < 0 || titleAnchor >= kAnchorCount) return;
  if (titleTruncate) return;                    // clipping title never reflows
  if (layout.counts[titleAnchor] == 0) return;  // no title placed

  const int tband = titleAnchor / 3;
  const int tcol = titleAnchor % 3;

  // The two other anchors in the title's band, nearest-column first (ties resolve
  // to the higher column so a centre title evicts the right corner before left).
  int neighbours[2];
  {
    int n = 0;
    for (int col = 2; col >= 0; col--) {
      if (col == tcol) continue;
      neighbours[n++] = tband * 3 + col;
    }
    // sort the (at most 2) neighbours by |col - tcol| ascending
    if (n == 2) {
      const int d0 = neighbours[0] % 3 - tcol;
      const int d1 = neighbours[1] % 3 - tcol;
      if ((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1)) {
        const int tmp = neighbours[0];
        neighbours[0] = neighbours[1];
        neighbours[1] = tmp;
      }
    }
  }

  int tStart, tEnd;
  clusterExtent(layout, titleAnchor, bandWidth, sepW, &tStart, &tEnd);

  for (int i = 0; i < 2; i++) {
    const int nb = neighbours[i];
    if (layout.counts[nb] == 0) continue;
    int nStart, nEnd;
    clusterExtent(layout, nb, bandWidth, sepW, &nStart, &nEnd);
    // Overlap with a one-separator breathing gap between clusters.
    if (tStart < nEnd + sepW && nStart < tEnd + sepW) {
      bumpCluster(layout, nb, destBandReserved, bandWidth, sepW);
      // Title width is fixed; its extent is unchanged after a neighbour leaves.
    }
  }
}

}  // namespace statusbar
