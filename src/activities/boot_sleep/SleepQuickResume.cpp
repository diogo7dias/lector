// SleepActivity's Quick Resume face: the page stays on the panel and only a moon is added.
// Split out of SleepActivity.cpp to keep each file under the token limit.
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepActivity.h"
#include "activities/reader/ReaderUtils.h"

namespace {

// A 32 px white tile in the bottom-left corner with a black crescent: the circle minus a
// smaller circle pushed up and right. Integer maths, no bitmap in flash.
void drawMoon(const GfxRenderer& renderer) {
  constexpr int TILE = 32;
  constexpr int MARGIN = 6;
  constexpr int R = 12;                     // outer circle
  constexpr int BITE_R = 10;                // the circle that is cut away
  constexpr int BITE_DX = 6, BITE_DY = -5;  // where it sits relative to the outer centre
  int top = 0, right = 0, bottom = 0, left = 0;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int x0 = left + MARGIN;
  const int y0 = renderer.getScreenHeight() - bottom - MARGIN - TILE;
  renderer.fillRect(x0, y0, TILE, TILE, false);
  const int cx = x0 + TILE / 2;
  const int cy = y0 + TILE / 2;
  for (int dy = -R; dy <= R; dy++) {
    for (int dx = -R; dx <= R; dx++) {
      const bool inMoon = dx * dx + dy * dy <= R * R;
      const int bx = dx - BITE_DX;
      const int by = dy - BITE_DY;
      if (inMoon && bx * bx + by * by > BITE_R * BITE_R) renderer.drawPixel(cx + dx, cy + dy, true);
    }
  }
}

}  // namespace

// Nothing is cleared and no popup is shown: the page IS the sleep screen. Only the moon
// differs from what the panel holds, so a differential FAST pass adds it without the clean
// pass that would flash the page away. main.cpp saves the frame once this returns.
void SleepActivity::renderQuickResumeScreen() const {
  renderer.waitRefreshComplete();
  if (APP_STATE.lastSleepFromReader) ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  drawMoon(renderer);
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  if (display.profile().isX3) {
    // The X3 controller still holds the displayed page, and its differential base is what
    // adds the moon without a flash; both lector's earlier Quick Resume and CrossPoint do this.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}
