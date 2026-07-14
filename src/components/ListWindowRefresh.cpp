#include "ListWindowRefresh.h"

#include <Logging.h>

#include "ListWindowPolicy.h"

namespace list_window {
namespace {
FrameSnapshot previousFrame;
FrameSnapshot currentFrame;
}  // namespace

void noteListDraw(const int x, const int y, const int width, const int height, const int rowHeight, const int pageItems,
                  const int itemCount, const int selectedIndex) {
  currentFrame.x = x;
  currentFrame.y = y;
  currentFrame.width = width;
  currentFrame.height = height;
  currentFrame.rowHeight = rowHeight;
  currentFrame.pageItems = pageItems;
  currentFrame.itemCount = itemCount;
  currentFrame.selectedIndex = selectedIndex;
  currentFrame.listDrawCalls++;
}

void invalidate() {
  previousFrame = FrameSnapshot{};
  currentFrame = FrameSnapshot{};
}

void present(const GfxRenderer& renderer, const HalDisplay::RefreshMode requested, const uint32_t extraHash) {
  currentFrame.extraHash = extraHash;
  currentFrame.valid = true;

  WindowRect window;
  if (requested == HalDisplay::FAST_REFRESH && planSelectionWindow(previousFrame, currentFrame, &window)) {
    LOG_DBG("LWIN", "Windowed list refresh (%d,%d %dx%d)", window.x, window.y, window.width, window.height);
    renderer.displayWindow(window.x, window.y, window.width, window.height);
  } else {
    renderer.displayBuffer(requested);
  }

  previousFrame = currentFrame;
  currentFrame = FrameSnapshot{};
}

}  // namespace list_window
