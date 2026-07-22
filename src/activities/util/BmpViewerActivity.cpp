#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "ImageViewerPolicy.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepPauseToggle.h"
#include "util/ButtonResponsePolicy.h"

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path,
                                     bool resultMode)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)), resultMode(resultMode) {}

void BmpViewerActivity::loadSiblingImages() {
  // Mark the attempt before touching storage. Empty, missing, and single-image
  // folders must not trigger another full directory scan on the next action.
  siblingsLoaded = true;
  siblingImages.clear();
  currentImageIndex = -1;

  if (filePath.empty()) return;

  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  size_t lastSlash = filePath.find_last_of('/');
  std::string fileName = (lastSlash != std::string::npos) ? filePath.substr(lastSlash + 1) : filePath;

  auto dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    return;
  }

  char name[500];
  for (auto file = dir.openNextFile(); file; file = dir.openNextFile()) {
    if (!file.isDirectory()) {
      file.getName(name, sizeof(name));
      if (name[0] != '.') {
        std::string fname(name);
        if (fname.length() >= 4 && fname.substr(fname.length() - 4) == ".bmp") {
          siblingImages.push_back(fname);
        }
      }
    }
    file.close();
  }
  dir.close();

  FsHelpers::sortFileList(siblingImages);

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == fileName) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  const bool inSleepDirs = crosspoint::sleep::isUnderSleepDirs(filePath);
  if (!siblingsLoaded && !filePath.empty() && image_viewer_policy::loadSiblingsOnEnter(resultMode, inSleepDirs)) {
    loadSiblingImages();
  }

  HalFile file;

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), PopupRefresh::Temporary);
  GUI.fillBottomProgress(renderer, 20);  // Initial 20% progress
  // 1. Open the file
  if (Storage.openFileForRead("BMP", filePath, file)) {
    Bitmap bitmap(file, true);

    // 2. Parse headers to get dimensions
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      int x, y;

      if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
        float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
        const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

        if (ratio > screenRatio) {
          // Wider than screen
          x = 0;
          y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
        } else {
          // Taller than screen
          x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
          y = 0;
        }
      } else {
        // Center small images
        x = (pageWidth - bitmap.getWidth()) / 2;
        y = (pageHeight - bitmap.getHeight()) / 2;
      }

      // 4. Prepare Rendering
      const bool showLazyHints =
          image_viewer_policy::showUnloadedNavigationHints(resultMode, inSleepDirs, siblingsLoaded);
      bool hasPrevious = showLazyHints || (siblingImages.size() > 1 && currentImageIndex > 0);
      bool hasNext = showLazyHints || (siblingImages.size() > 1 && currentImageIndex != -1 &&
                                       currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

      // Confirm is contextual: a wallpaper already living in /sleep or /sleep pause
      // gets a one-press move to the other folder; anywhere else it sets the cover.
      const char* confirmLabel = inSleepDirs ? (filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP)
                                                                                        : tr(STR_SLEEP_MOVE_TO_PAUSE))
                                             : tr(STR_SET_SLEEP_COVER);
      const auto labels =
          mappedInput.mapLabels(tr(STR_BACK), confirmLabel, (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));

      GUI.fillBottomProgress(renderer, 50);

      renderer.clearScreen();
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

      // Draw UI hints on the base layer
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      // Single pass for non-grayscale images

      renderer.present(RefreshIntent::MenuNav);

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Invalid BMP File");
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.present(RefreshIntent::CleanFrame);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, "Could not open file");
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.present(RefreshIntent::CleanFrame);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  // The live browser paints one clean list frame when this child returns. Avoid
  // an extra blank cleanup pass before that frame, which doubled move latency.
  if (resultMode) return;
  renderer.clearScreen();
  renderer.present(RefreshIntent::CleanFrame);
}

void BmpViewerActivity::returnToBrowser(const bool removed) {
  if (resultMode) {
    setResult(FilePathResult{removed ? std::string() : filePath, removed ? filePath : std::string()});
    finish();
  } else {
    activityManager.goToFileBrowser(filePath);
  }
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP), PopupRefresh::Temporary);

  bool success = false;
  HalFile inFile, outFile;
  if (Storage.openFileForRead("BMP", filePath, inFile)) {
    if (Storage.openFileForWrite("BMP", "/sleep.bmp", outFile)) {
      char buffer[2048];
      int bytesRead;
      success = true;
      while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
        if (outFile.write(buffer, bytesRead) != bytesRead) {
          success = false;
          break;
        }
      }
      outFile.close();
    }
    inFile.close();
  }

  if (success) {
    SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::moveSleepPause() {
  // Move to the other folder and immediately return to the browser at this file's
  // old slot (the next image is then selected). Passing the original path lets the
  // browser land there even though the file is now gone from this folder.
  // A same-named file in the destination makes SdFat's rename fail (it never
  // overwrites) — surface that and stay put instead of pretending it moved.
  const auto res = crosspoint::sleep::toggleSleepPause(filePath);
  if (!res.ok) {
    GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
    return;
  }
  returnToBrowser(true);
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    returnToBrowser(false);
    return;
  }

  if (crosspoint::sleep::isUnderSleepDirs(filePath)) {
    const bool moveTriggered = button_response::imageMoveTrigger() == button_response::Trigger::Press
                                   ? mappedInput.wasPressed(MappedInputManager::Button::Confirm)
                                   : mappedInput.wasReleased(MappedInputManager::Button::Confirm);
    if (moveTriggered) {
      moveSleepPause();
      return;
    }
  } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    doSetSleepCover();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left) ||
      mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    const bool siblingsLoadedBeforeAction = siblingsLoaded;
    if (!siblingsLoaded) loadSiblingImages();
    bool navigationMoved = false;
    if (siblingImages.size() > 1 && currentImageIndex > 0) {
      currentImageIndex--;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[currentImageIndex];
      onEnter();
      navigationMoved = true;
    }
    if (image_viewer_policy::refreshHintsAfterNavigation(siblingsLoadedBeforeAction, navigationMoved)) onEnter();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right) ||
      mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    const bool siblingsLoadedBeforeAction = siblingsLoaded;
    if (!siblingsLoaded) loadSiblingImages();
    bool navigationMoved = false;
    if (siblingImages.size() > 1 && currentImageIndex != -1 &&
        currentImageIndex < static_cast<int>(siblingImages.size()) - 1) {
      currentImageIndex++;
      std::string dirPath = FsHelpers::extractFolderPath(filePath);
      if (dirPath.back() != '/') dirPath += "/";
      filePath = dirPath + siblingImages[currentImageIndex];
      onEnter();
      navigationMoved = true;
    }
    if (image_viewer_policy::refreshHintsAfterNavigation(siblingsLoadedBeforeAction, navigationMoved)) onEnter();
    return;
  }
}
