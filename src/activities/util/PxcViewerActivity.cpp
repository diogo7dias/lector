#include "PxcViewerActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <utility>

#include "ConfirmationActivity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepPauseToggle.h"

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("PxcViewer", renderer, mappedInput), filePath(std::move(filePath)) {}

void PxcViewerActivity::render() {
  // Button hints for the viewer: Back / Move (Confirm) / Delete (Left). The move
  // label is empty for files outside /sleep so Confirm isn't a hidden action.
  const char* moveLabel =
      crosspoint::sleep::isUnderSleepDirs(filePath)
          ? (filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP) : tr(STR_SLEEP_MOVE_TO_PAUSE))
          : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), moveLabel, tr(STR_DELETE), "");
  // Bake the hints into every grayscale pass so they composite solid over the
  // wallpaper. Drawing them AFTER renderPxcSleepScreen's own refresh would need a
  // separate partial refresh, which accumulates charge -> ghosting on X3; passing
  // them as the per-pass overlay avoids that entirely (one grayscale refresh total).
  const auto drawHints = [&]() { GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4); };
  // Render byte-for-byte like the lock screen (3-pass grayscale, panel-sized), now
  // with the hint band baked in. Returns false when the .pxc does not match this
  // screen (+/-1 px) or fails to open; only then do we show an error frame.
  if (!renderPxcSleepScreen(renderer, filePath, drawHints)) {
    const auto pageHeight = renderer.getScreenHeight();
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_PXC_WRONG_SIZE));
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
  // Left button deletes the wallpaper, behind a confirmation. On confirm+success we
  // return to the browser at this file's old slot; on cancel/failure the viewer's
  // onEnter() re-renders the image (ActivityManager resumes the parent on return).
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    const std::string name = filePath.substr(filePath.rfind('/') + 1);
    startActivityForResult(
        std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "), name),
        [this](const ActivityResult& res) {
          if (!res.isCancelled) {
            if (Storage.remove(filePath.c_str())) {
              activityManager.goToFileBrowser(filePath);
            } else {
              LOG_ERR("PXC", "Failed to delete: %s", filePath.c_str());
            }
          }
        });
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
