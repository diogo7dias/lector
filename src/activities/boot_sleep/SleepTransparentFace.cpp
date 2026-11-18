// SleepActivity's Transparent face and the popup that keeps the page under it.
// Split out of SleepActivity.cpp to keep each file under the token limit.
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepActivity.h"
#include "SleepFacePaint.h"
#include "SleepOverlay.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"

// The popup is a banner over the top of the page; the framebuffer is put back as it was
// before the face draws, because Transparent keeps the page. The BW buffer is kept in
// chunks (GfxRenderer::storeBwBuffer), so no 48 KB block is needed. When it cannot be kept
// the popup is skipped rather than left in the page.
void SleepActivity::showPopupKeepingPage() const {
  if (!renderer.storeBwBuffer()) {
    LOG_ERR("SLP", "OOM: no room to keep the page under the popup, skipping it");
    return;
  }
  if (APP_STATE.lastSleepFromReader) ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
  GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  renderer.restoreBwBuffer();
}

// The overlay over the page. Nothing usable on the card means the Lector face.
void SleepActivity::renderTransparentCustomSleepScreen() const {
  SleepOverlay overlay;
  if (!overlay.prepare(renderer)) return renderDefaultSleepScreen();
  // BW pass into the page already in the framebuffer.
  if (!overlay.draw(renderer)) {
    LOG_ERR("SLP", "Overlay draw failed");
    return renderDefaultSleepScreen();
  }
  // Then the panel, the way every bitmap face does: a grey base and the two planes where the
  // panel can draw grey, one HALF pass where it cannot (sleep_face::planFor).
  const sleep_face::PaintPlan plan =
      sleep_face::planFor(sleep_face::Face::Wallpaper, display.profile().panelHasGray, display.profile());
  if (!plan.grayscalePlanes) {
    renderer.displayBuffer(plan.base);
    return;
  }
  renderer.displayGrayscaleBase(plan.base);
  sleep_face::paintGrayscalePlanes(renderer, [&] { return overlay.draw(renderer); }, [](GfxRenderer::RenderMode) {});
}
