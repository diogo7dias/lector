#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "fontIds.h"
#include "images/Logo120.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // 4-block segmented loader below the brand. Each block fills on its own e-ink
  // refresh, so the panel shows the bar filling one block at a time before Home
  // takes over. This blocks in onEnter for ~4 refreshes by design (branded boot
  // animation); the boot routing swaps in Home right after.
  constexpr int kSegments = 4;
  constexpr int segW = 44;
  constexpr int segH = 18;
  constexpr int segGap = 6;
  const int totalW = kSegments * segW + (kSegments - 1) * segGap;
  const int segX0 = (pageWidth - totalW) / 2;
  const int segY = pageHeight / 2 + 100;

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  for (int i = 0; i < kSegments; i++) {
    renderer.drawRect(segX0 + i * (segW + segGap), segY, segW, segH, 2, true);  // empty block outlines
  }
  renderer.displayBuffer();

  // Fill one block per refresh (the refresh time itself paces the animation).
  for (int i = 0; i < kSegments; i++) {
    renderer.fillRect(segX0 + i * (segW + segGap), segY, segW, segH, true);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}
