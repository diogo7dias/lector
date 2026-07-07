#include "PxcViewerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("PxcViewer", renderer, mappedInput), filePath(std::move(filePath)) {}

void PxcViewerActivity::onEnter() {
  Activity::onEnter();

  // renderPxcSleepScreen draws the whole frame (3-pass grayscale) and pushes it.
  // It returns false when the .pxc does not match this screen (+/-1 px) or fails
  // to open — show that instead of a blank/garbage screen.
  if (!renderPxcSleepScreen(renderer, filePath)) {
    const auto pageHeight = renderer.getScreenHeight();
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_PXC_WRONG_SIZE));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void PxcViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void PxcViewerActivity::loop() {
  Activity::loop();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }
}
