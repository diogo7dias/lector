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
  // Forward pages turned since this book was opened. -1 hides the item in readers
  // without a session counter.
  int sessionPages = -1;
  // Pages after this one still inside the paragraph this page starts in, so 0 means
  // a new paragraph begins on the next page. -1 = the chapter cannot answer (no
  // paragraph table), which hides the item rather than showing a wrong 0.
  int paragraphPagesLeft = -1;
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

// Thickness actually drawn. An outlined bar spends 2px per side on the frame, so
// a slim bar would have no room left to show a partial fill; it is nudged to 5px.
// Every site that draws OR reserves space for a bar must call this, or the
// reserved band and the drawn bar disagree.
inline int statusBarDrawThicknessPx(const uint8_t thickness, const bool outline) {
  const int px = statusBarThicknessPx(thickness);
  return outline && px < 5 ? 5 : px;
}

// ---------------------------------------------------------------------------
// Reflow (v2): a truncate-OFF ("greedy") title wants its full width. If a
// same-band neighbour would overlap it, that neighbour is bumped out to the
// opposite band and either sits at a free anchor, joins an occupied-but-roomy
// one, or (if nothing fits) is hidden.
//
// This lives here as PURE integer/pointer math on fixed stack arrays — no
// GfxRenderer, no Arduino, no heap — so it runs on the lock-holding, stack-tight
// render task AND is host-testable (see test/statusbar_reflow).
//
// Anchor index: 0=TL 1=TC 2=TR 3=BL 4=BC 5=BR. band = idx/3 (0 top, 1 bottom),
// column = idx%3 (0 left, 1 centre, 2 right).
// ---------------------------------------------------------------------------
namespace statusbar {

// One drawn item. `text` points at a caller-owned buffer (never copied here);
// `isBattery` selects the icon draw. POD so buckets can be relocated by value.
struct Seg {
  const char* text = nullptr;
  int width = 0;
  bool isBattery = false;
};

// The anchored items of the bar, declared once, in the order the renderer places them.
// X(id, blockField, chapterOnly): blockField is the StatusBarBlock anchor member;
// chapterOnly items hide on a chapterless book. The renderer's push loop and the band
// height check (UITheme's sbBandHasText) both expand this list, so a new item that is
// drawn always reserves its band. A new item also needs its field in
// READER_STATUS_BAR_FIELDS, a settings row, and its text in drawStatusBarV2.
#define STATUS_BAR_ITEMS(X)               \
  X(Battery, batteryPos, false)           \
  X(Clock, clockPos, false)               \
  X(Title, titlePos, false)               \
  X(Page, pagePos, false)                 \
  X(BookPct, bookPctPos, false)           \
  X(ChapterPct, chapterPctPos, true)      \
  X(ChapterNum, chapterNumPos, true)      \
  X(SessionPages, sessionPagesPos, false) \
  X(ParaPages, paraPagesPos, false)

#define SB_ITEM_ENUM(id, field, chapterOnly) id,
enum Item : uint8_t { STATUS_BAR_ITEMS(SB_ITEM_ENUM) kItemCount };
#undef SB_ITEM_ENUM

constexpr int kAnchorCount = 6;
// Every item can target the same anchor, so an anchor holds them all and none is dropped.
constexpr int kMaxPerAnchor = kItemCount;

// Left and right x of the text band: the oriented screen width less the viewable margins
// and the theme's bar margin. The title wrap that reserves height (UITheme) and the draw
// (BaseTheme) both take the band from here, so they cannot disagree on its width.
inline void bandEdges(int screenW, int marginL, int marginR, int hMargin, int* left, int* right) {
  *left = hMargin + marginL + 1;
  *right = screenW - hMargin - marginR;
}

// Six anchors, each holding up to kMaxPerAnchor co-located segments in
// enable-order. `counts[a]` is how many of `buckets[a]` are live.
struct BarLayout {
  Seg buckets[kAnchorCount][kMaxPerAnchor];
  int counts[kAnchorCount] = {0, 0, 0, 0, 0, 0};
};

// Total drawn width of one anchor's cluster: segment widths plus one `sepW`
// separator advance between each pair. 0 for an empty anchor.
int clusterWidth(const BarLayout& layout, int anchor, int sepW);

// Reflow around a greedy title. No-op when the title is absent (titleAnchor < 0),
// truncation is ON, or the title's anchor is empty. Same-band neighbours that
// overlap the full-width title are bumped to the OPPOSITE band, searched
// same-side-first (left item -> [oppL,oppC,oppR]; right -> [oppR,oppC,oppL];
// centre -> [oppC,oppL,oppR]); a bumped cluster sits at the first free anchor,
// else joins the first occupied anchor that still leaves the band's other two
// columns room, else is hidden. `destBandReserved` MUST be false when the
// opposite band has no native text (its height is not reserved) — then bumped
// items are hidden rather than drawn into unreserved space.
//
// Items sharing the title's OWN anchor are moved out the same way, but only when the
// combined cluster is wider than the band. Left in place they would be drawn as one
// centred cluster with no clipping, which is how a long chapter title beside a page count
// used to run off both edges. `titleSegIndex` says which segment of that anchor is the
// title; it is rarely 0, because battery and clock are placed before it.
void reflowTitle(BarLayout& layout, int titleAnchor, int titleSegIndex, bool titleTruncate, int bandWidth, int sepW,
                 bool destBandReserved);

}  // namespace statusbar
