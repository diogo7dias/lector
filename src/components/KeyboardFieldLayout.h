#pragma once

// Where the keyboard's text field sits, and where its cursor and password
// toggle go inside it. Pure arithmetic, so it is testable without a panel, and
// so the paint and the tap test cannot drift apart: both used to compute the
// margin, the toggle reserve and the text area width from the same expressions
// written out twice, which is a bug waiting for one of the two to be edited.
namespace keyboard_field {

struct Metrics {
  int pageWidth = 0;
  // Taken off both sides on hardware that prints the side-button hints.
  int sideHintsWidth = 0;
  bool reserveSideHints = false;
  // Share of the usable width the field spans, from the theme.
  int widthPercent = 100;
  // Width of the password toggle plus its gap; 0 on a field that has none.
  int toggleReserve = 0;
};

// Inset from either edge of the page to the field.
inline int marginFor(const Metrics& metrics) {
  int available = metrics.pageWidth;
  if (metrics.reserveSideHints) available -= 2 * metrics.sideHintsWidth;
  const int span = available * metrics.widthPercent / 100;
  const int margin = (metrics.pageWidth - span) / 2;
  return margin < 0 ? 0 : margin;
}

// Width the text itself may take, which is the field minus whatever the toggle
// holds back.
inline int textWidthFor(const Metrics& metrics) {
  const int width = metrics.pageWidth - 2 * marginFor(metrics) - metrics.toggleReserve;
  return width < 1 ? 1 : width;
}

// Where a line of the given width starts: hard against the margin, or centred
// in the text area when the theme asks for centred entry.
inline int lineStartX(const Metrics& metrics, const int lineWidth, const bool centred) {
  const int margin = marginFor(metrics);
  if (!centred) return margin;
  const int slack = textWidthFor(metrics) - lineWidth;
  return margin + (slack > 0 ? slack / 2 : 0);
}

// The toggle sits at the right edge of the field, outside the text area it
// reserved.
inline int toggleX(const Metrics& metrics, const int toggleWidth) {
  return metrics.pageWidth - marginFor(metrics) - toggleWidth;
}

// Top of the field, under the header.
inline int fieldTop(const int topPadding, const int headerHeight, const int verticalSpacing,
                    const int keyboardVerticalOffset) {
  // Four extra spacings of air: the field is the thing being edited, and it
  // reads as that only when it is not tucked under the header.
  return topPadding + headerHeight + verticalSpacing * 5 + keyboardVerticalOffset;
}

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

// The block cursor: one pixel of air on each side of the character it covers,
// so the glyph under it is not touching the fill.
inline Rect blockCursor(const int cursorX, const int cursorY, const int charWidth, const int lineHeight,
                        const int padding = 1) {
  return Rect{cursorX - padding, cursorY, charWidth + padding * 2, lineHeight};
}

}  // namespace keyboard_field
