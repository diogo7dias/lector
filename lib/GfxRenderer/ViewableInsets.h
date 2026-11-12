#pragma once

// Bezel-covered edge insets, expressed in the renderer's Portrait frame, and the
// rotation that maps them onto the active screen orientation.
//
// Pure integer math with no Arduino / BoardConfig dependency so it is
// host-testable (see test/viewable_insets); GfxRenderer feeds it the running
// board's measured insets.
namespace gfx {

struct ViewableInsets {
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;

  bool operator==(const ViewableInsets& other) const {
    return top == other.top && right == other.right && bottom == other.bottom && left == other.left;
  }
};

// The most a reader may calibrate any one edge to, in pixels (Settings > Display > Screen
// Calibration). A stored value past it is treated as no calibration at all.
inline constexpr int MAX_CALIBRATED_INSET = 32;
inline constexpr bool validCalibration(const ViewableInsets& insets) {
  return insets.top >= 0 && insets.top <= MAX_CALIBRATED_INSET && insets.right >= 0 &&
         insets.right <= MAX_CALIBRATED_INSET && insets.bottom >= 0 && insets.bottom <= MAX_CALIBRATED_INSET &&
         insets.left >= 0 && insets.left <= MAX_CALIBRATED_INSET;
}

// Orientation index matching GfxRenderer::Orientation.
enum class InsetOrientation { Portrait = 0, LandscapeClockwise, PortraitInverted, LandscapeCounterClockwise };

// Rotate panel-native insets into screen-space top/right/bottom/left.
constexpr ViewableInsets orientInsets(const ViewableInsets& panel, InsetOrientation orientation) {
  switch (orientation) {
    case InsetOrientation::LandscapeClockwise:
      return {panel.left, panel.top, panel.right, panel.bottom};
    case InsetOrientation::PortraitInverted:
      return {panel.bottom, panel.left, panel.top, panel.right};
    case InsetOrientation::LandscapeCounterClockwise:
      return {panel.right, panel.bottom, panel.left, panel.top};
    case InsetOrientation::Portrait:
    default:
      return panel;
  }
}

}  // namespace gfx
