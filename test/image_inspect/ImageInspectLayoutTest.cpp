#include <gtest/gtest.h>

#include "src/activities/reader/ImageInspectLayout.h"

namespace {

using image_inspect::DoubleTapTracker;
using image_inspect::Rect;
using image_inspect::ZoomView;

TEST(ImageInspectLayout, RotatesWideImageOnPortraitPanelOnly) {
  // Wide image on portrait 480x800 panel turns 90 degrees so the long axis aligns.
  EXPECT_TRUE(image_inspect::shouldRotateWide(1807, 736, 480, 800));
  EXPECT_TRUE(image_inspect::shouldRotateWide(350, 250, 480, 800));

  // Portrait or square images stay upright on a portrait panel.
  EXPECT_FALSE(image_inspect::shouldRotateWide(400, 600, 480, 800));
  EXPECT_FALSE(image_inspect::shouldRotateWide(400, 400, 480, 800));

  // Already on a landscape panel: wide images already fit the long axis.
  EXPECT_FALSE(image_inspect::shouldRotateWide(1807, 736, 800, 480));
  EXPECT_FALSE(image_inspect::shouldRotateWide(0, 100, 480, 800));
}

TEST(ImageInspectLayout, RotateOrientation90CyclesClockwise) {
  EXPECT_EQ(image_inspect::rotateOrientation90(0), 1);
  EXPECT_EQ(image_inspect::rotateOrientation90(1), 2);
  EXPECT_EQ(image_inspect::rotateOrientation90(2), 3);
  EXPECT_EQ(image_inspect::rotateOrientation90(3), 0);
}

TEST(ImageInspectLayout, FitToPanelPreservesAspectAndCenters) {
  // Wide 1807x736 fitted to rotated 800x480 full panel (no margins, no status bar).
  const Rect wide = image_inspect::fitToPanel(1807, 736, 800, 480);
  EXPECT_EQ(wide.width, 800);
  EXPECT_EQ(wide.height, 326);
  EXPECT_EQ(wide.x, 0);
  EXPECT_EQ(wide.y, (480 - 326) / 2);

  // Portrait 1200x1500 fitted to 480x800 panel.
  const Rect tall = image_inspect::fitToPanel(1200, 1500, 480, 800);
  EXPECT_EQ(tall.width, 480);
  EXPECT_EQ(tall.height, 600);
  EXPECT_EQ(tall.x, 0);
  EXPECT_EQ(tall.y, 100);

  // Small 350x250 scaled up to fill rotated 800x480 panel.
  const Rect small = image_inspect::fitToPanel(350, 250, 800, 480);
  EXPECT_EQ(small.height, 480);
  EXPECT_EQ(small.width, 672);
  EXPECT_EQ(small.x, (800 - 672) / 2);
  EXPECT_EQ(small.y, 0);
}

TEST(ImageInspectLayout, Zoom2xCentersAndClampsPan) {
  // 480x600 fitted image at 2x is 960x1200 inside a 480x800 panel.
  const ZoomView centered = image_inspect::centerZoomedView(480, 600, 480, 800);
  EXPECT_EQ(centered.zoomedW, 960);
  EXPECT_EQ(centered.zoomedH, 1200);
  EXPECT_EQ(centered.panX, 240);
  EXPECT_EQ(centered.panY, 200);

  // Pan clamping prevents exposing blank margins past the zoomed image bounds.
  const ZoomView neg = image_inspect::makeZoomedView(480, 600, 480, 800, -50, -90);
  EXPECT_EQ(neg.panX, 0);
  EXPECT_EQ(neg.panY, 0);

  const ZoomView over = image_inspect::makeZoomedView(480, 600, 480, 800, 9999, 9999);
  EXPECT_EQ(over.panX, 480);
  EXPECT_EQ(over.panY, 400);

  // Short axis (200*2 = 400 < 480) stays centered with pan = 0 and offset > 0.
  const ZoomView narrow = image_inspect::makeZoomedView(200, 300, 480, 800, 100, 100);
  EXPECT_EQ(narrow.panX, 0);
  EXPECT_EQ(narrow.panY, 0);
  EXPECT_EQ(narrow.offsetX, 40);
  EXPECT_EQ(narrow.offsetY, 100);
}

TEST(ImageInspectLayout, HitTestMatchesEnclosingImageRect) {
  const Rect rects[2] = {
      {65, 167, 350, 250},
      {40, 450, 400, 200},
  };
  EXPECT_EQ(image_inspect::hitTestRect(rects, 2, 65, 167), 0);
  EXPECT_EQ(image_inspect::hitTestRect(rects, 2, 414, 416), 0);
  EXPECT_EQ(image_inspect::hitTestRect(rects, 2, 415, 417), -1);
  EXPECT_EQ(image_inspect::hitTestRect(rects, 2, 200, 500), 1);
  EXPECT_EQ(image_inspect::hitTestRect(rects, 2, 10, 10), -1);
}

TEST(ImageInspectLayout, DoubleTapRequiresQuickSecondTapOnSameImage) {
  DoubleTapTracker tracker;
  EXPECT_FALSE(tracker.recordTap(1000, 200, 300, 0));
  EXPECT_TRUE(tracker.recordTap(1300, 210, 305, 0));
  // Resets after firing so a third tap starts a fresh sequence.
  EXPECT_FALSE(tracker.recordTap(1450, 210, 305, 0));

  // Too slow (> 500 ms) does not fire.
  EXPECT_FALSE(tracker.recordTap(2100, 210, 305, 0));

  // Too far (> 48 px) does not fire.
  EXPECT_FALSE(tracker.recordTap(2200, 320, 305, 0));

  // Different image does not fire.
  EXPECT_FALSE(tracker.recordTap(2300, 320, 305, 1));
}

}  // namespace
