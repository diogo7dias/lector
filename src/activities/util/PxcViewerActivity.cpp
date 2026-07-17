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
#include "util/ButtonResponsePolicy.h"
#include "util/FavoriteImage.h"

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath,
                                     bool resultMode)
    : Activity("PxcViewer", renderer, mappedInput),
      filePath(std::move(filePath)),
      launchPath(this->filePath),
      resultMode(resultMode) {}

void PxcViewerActivity::returnToBrowser(bool removed) {
  if (resultMode) {
    // Hand control back to the still-alive browser. Empty path = the file left this
    // folder (moved/deleted); otherwise the current (possibly renamed) path so the
    // browser patches that one row in place — no folder rescan.
    const bool changed = removed || filePath != launchPath;
    setResult(FilePathResult{removed ? std::string() : filePath, changed ? launchPath : std::string()});
    finish();
  } else {
    // ReaderActivity-launched path: rebuild the browser (it was replaced away). The
    // browser's onEnter re-derives the folder and lands on this file's old slot.
    activityManager.goToFileBrowser(filePath);
  }
}

void PxcViewerActivity::render() {
  // Button hints for the viewer: Back / Move (Confirm) / Delete (Left) / Fav (Right).
  // The move label is empty for files outside /sleep so Confirm isn't a hidden action.
  const char* moveLabel =
      crosspoint::sleep::isUnderSleepDirs(filePath)
          ? (filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP) : tr(STR_SLEEP_MOVE_TO_PAUSE))
          : "";
  const char* favLabel = FavoriteImage::isFavoritePath(filePath) ? tr(STR_UNFAV) : tr(STR_FAV);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), moveLabel, tr(STR_DELETE), favLabel);
  // Solid black and white controls must be present in the visible BW base. The
  // later grayscale planes can shade existing pixels but cannot create new BW
  // controls, so use the reliable every-pass path.
  const auto drawHints = [&]() { GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4); };
  // Render byte-for-byte like the lock screen (3-pass grayscale, panel-sized), now
  // with the hint band baked in. Returns false when the .pxc does not match this
  // screen (+/-1 px) or fails to open; only then do we show an error frame.
  // Blank FULL pass first: the viewer paints straight over the file-browser
  // list, and the calibrated differential HALF/graybase passes leave the old
  // rows ghosting through (device photos, 2026-07-16/17). A FULL *base* pass
  // is not enough — on X3 the graybase waveform is fixed and only the HALF
  // fallback path resyncs — so deep-clean the panel explicitly, then run the
  // unchanged render sequence.
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
  if (!renderPxcSleepScreen(renderer, filePath, drawHints, true, true, pxcViewerOverlayTiming())) {
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
  // The still-alive browser performs the one required cleanup paint. Skipping
  // this intermediate blank frame makes review-and-move a single panel update.
  if (resultMode) return;
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void PxcViewerActivity::loop() {
  Activity::loop();
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    returnToBrowser(false);
    return;
  }
  // Left button deletes the wallpaper, behind a confirmation. On confirm+success we
  // return to the browser at this file's old slot; on cancel/failure the viewer's
  // onEnter() re-renders the image (ActivityManager resumes the parent on return).
  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    const std::string name = filePath.substr(filePath.rfind('/') + 1);
    startActivityForResult(
        makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "), name),
        [this](const ActivityResult& res) {
          if (!res.isCancelled) {
            if (Storage.remove(filePath.c_str())) {
              returnToBrowser(true);
            } else {
              LOG_ERR("PXC", "Failed to delete: %s", filePath.c_str());
            }
          }
        });
    return;
  }
  // Right button toggles favorite by renaming the file (adds/strips the _F suffix),
  // then returns to the /sleep browser (the file stays in the folder, just renamed).
  // returnToBrowser(false) patches that one row in place — no folder rescan. On a
  // rename failure it stays in the viewer and re-renders.
  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    const bool makeFavorite = !FavoriteImage::isFavoritePath(filePath);
    std::string updated;
    if (FavoriteImage::setFavorite(filePath, makeFavorite, &updated) == FavoriteImage::SetFavoriteResult::Success) {
      filePath = updated;
      returnToBrowser(false);
      return;
    }
    render();
    return;
  }
  // Confirm moves the wallpaper to the other folder and immediately returns to the
  // browser at this file's old slot (the next image is then selected). Passing the
  // original path lets the browser land there even though the file is now gone.
  // A same-named file in the destination makes the rename fail (SdFat never
  // overwrites) — surface that and stay put instead of pretending it moved.
  const bool moveTriggered = button_response::imageMoveTrigger() == button_response::Trigger::Press
                                 ? mappedInput.wasPressed(MappedInputManager::Button::Confirm)
                                 : mappedInput.wasReleased(MappedInputManager::Button::Confirm);
  if (moveTriggered && crosspoint::sleep::isUnderSleepDirs(filePath)) {
    const auto res = crosspoint::sleep::toggleSleepPause(filePath);
    if (!res.ok) {
      GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
      return;
    }
    returnToBrowser(true);
    return;
  }
}
