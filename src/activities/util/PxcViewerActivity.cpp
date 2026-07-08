#include "PxcViewerActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <utility>

#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepPauseToggle.h"

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("PxcViewer", renderer, mappedInput), filePath(std::move(filePath)) {}

void PxcViewerActivity::render() {
  // Render byte-for-byte like the lock screen: renderPxcSleepScreen owns the whole
  // frame (3-pass grayscale + its own refresh), so nothing is drawn or refreshed on
  // top of it — the preview then looks exactly as it will when the device sleeps.
  // It returns false when the .pxc does not match this screen (+/-1 px) or fails to
  // open; only then do we show an error frame with hints.
  if (!renderPxcSleepScreen(renderer, filePath)) {
    const auto pageHeight = renderer.getScreenHeight();
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_PXC_WRONG_SIZE));
    // The move action stays available on the error frame (a wrong-size file is
    // exactly one you might want out of /sleep) — label it so Confirm isn't a
    // hidden action here.
    const char* moveLabel =
        crosspoint::sleep::isUnderSleepDirs(filePath)
            ? (filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP) : tr(STR_SLEEP_MOVE_TO_PAUSE))
            : "";
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), moveLabel, "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void PxcViewerActivity::onEnter() {
  Activity::onEnter();
  render();
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
  // Confirm moves the wallpaper to the other folder and immediately returns to the
  // browser at this file's old slot (the next image is then selected). Passing the
  // original path lets the browser land there even though the file is now gone.
  // A same-named file in the destination makes the rename fail (SdFat never
  // overwrites) — surface that and stay put instead of pretending it moved.
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) && crosspoint::sleep::isUnderSleepDirs(filePath)) {
    const std::string original = filePath;
    const auto res = crosspoint::sleep::toggleSleepPause(filePath);
    if (!res.ok) {
      GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
      return;
    }
    activityManager.goToFileBrowser(original);
    return;
  }
}
