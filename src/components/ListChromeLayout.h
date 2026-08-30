#pragma once

// Where a list screen's chrome sits: the title band, whatever a screen puts
// under it (a wrapped book header, a hint line, a note), and whatever it puts
// above the button hints (a footnote). Pure arithmetic, so the reserved height
// and the bands are the same number by construction.
//
// Before this, six screens each worked it out again: one inflated its content
// margin by a tab-bar height to make room for a sub-header, another placed a
// footnote by subtracting a line height from the screen bottom, a third put its
// book header at hand-written offsets. Every one of them was a place a type-size
// change could push text under a band.
namespace list_chrome {

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct Metrics {
  int screenWidth = 0;
  int screenHeight = 0;
  int topPadding = 0;
  int headerHeight = 0;
  int subHeaderHeight = 0;  // the tab-bar band a sub-header borrows
  int lineHeight = 0;       // one line in the small face
  int spacing = 0;          // air between bands
  int hintsHeight = 0;
};

// What the screen asked for. Counts rather than flags, because the book header
// wraps to as many lines as the title needs.
struct Content {
  bool hasHeader = false;
  bool hasSubHeader = false;
  int headerLines = 0;  // centred lines under the title band
  int noteLines = 0;      // left-aligned lines under everything above
  int footnoteLines = 0;  // lines between the body and the button hints
};

struct Bands {
  Rect header;
  Rect subHeader;
  Rect headerLines;
  Rect note;
  Rect footnote;
  // What the list itself gets: the first row starts at contentTop, and the last
  // one ends at contentBottom.
  int contentTop = 0;
  int contentBottom = 0;
};

inline Bands bandsFor(const Metrics& metrics, const Content& content) {
  Bands bands;
  int y = metrics.topPadding;
  if (content.hasHeader) {
    bands.header = Rect{0, y, metrics.screenWidth, metrics.headerHeight};
    y += metrics.headerHeight;
  }
  if (content.hasSubHeader) {
    bands.subHeader = Rect{0, y, metrics.screenWidth, metrics.subHeaderHeight};
    y += metrics.subHeaderHeight;
  }
  if (content.headerLines > 0) {
    const int height = content.headerLines * metrics.lineHeight;
    bands.headerLines = Rect{0, y, metrics.screenWidth, height};
    y += height;
  }
  if (content.noteLines > 0) {
    const int height = content.noteLines * metrics.lineHeight;
    bands.note = Rect{0, y, metrics.screenWidth, height};
    y += height;
  }
  bands.contentTop = y + metrics.spacing;

  int bottom = metrics.screenHeight - metrics.hintsHeight;
  if (content.footnoteLines > 0) {
    const int height = content.footnoteLines * metrics.lineHeight;
    bottom -= height;
    bands.footnote = Rect{0, bottom, metrics.screenWidth, height};
  }
  bands.contentBottom = bottom - metrics.spacing;
  // A screen with more chrome than panel keeps an empty content band rather
  // than an inside-out one.
  if (bands.contentBottom < bands.contentTop) bands.contentBottom = bands.contentTop;
  return bands;
}

// The insets a screen's content margin needs, which is the same arithmetic seen
// from the other end.
inline int topInsetFor(const Metrics& metrics, const Content& content) {
  return bandsFor(metrics, content).contentTop;
}

inline int bottomInsetFor(const Metrics& metrics, const Content& content) {
  const Bands bands = bandsFor(metrics, content);
  return metrics.screenHeight - bands.contentBottom;
}

}  // namespace list_chrome
