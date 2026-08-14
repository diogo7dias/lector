#include "BmpViewerActivity.h"

#include <Bitmap.h>
#include <Epub/converters/PngToFramebufferConverter.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepPauseToggle.h"
#include "util/FavoriteImage.h"

namespace {
constexpr char CUSTOM_SLEEP_ROOT_BMP[] = "/sleep.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_BMP[] = "/sleep-overlay.bmp";
constexpr char TRANSPARENT_SLEEP_ROOT_PNG[] = "/sleep-overlay.png";
}  // namespace

BmpViewerActivity::BmpViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string path)
    : Activity("BmpViewer", renderer, mappedInput), filePath(std::move(path)) {}

void BmpViewerActivity::loadSiblingImages() {
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
        const std::string_view fname(name);
        if (FsHelpers::hasBmpExtension(fname) || FsHelpers::hasPngExtension(fname)) {
          // Bounded on purpose: this folder can hold thousands of images, and an
          // unbounded name list exhausts the heap into a throwing allocation.
          if (!siblingImages.push(fname)) break;
        }
      }
    }
    file.close();
  }
  dir.close();

  siblingImages.sortByC([](const char* a, const char* b) { return FsHelpers::fileListLessC(a, b); });

  for (size_t i = 0; i < siblingImages.size(); ++i) {
    if (siblingImages[i] == std::string_view(fileName)) {
      currentImageIndex = static_cast<int>(i);
      break;
    }
  }
}

void BmpViewerActivity::drawHints() {
  const bool hasPrevious = (siblingImages.size() > 1 && currentImageIndex > 0);
  const bool hasNext = (siblingImages.size() > 1 && currentImageIndex != -1 &&
                        currentImageIndex < static_cast<int>(siblingImages.size()) - 1);

  // Inside a sleep folder the front buttons are triage, exactly as in the .pxc
  // viewer, so the two image viewers do not want different fingers. Siblings move
  // to Up/Down there; outside those folders nothing has changed.
  const bool triage = crosspoint::sleep::isUnderSleepDirs(filePath);
  const char* favLabel = FavoriteImage::isFavoritePath(filePath) ? tr(STR_UNFAV) : tr(STR_FAV);
  const char* pauseLabel =
      filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP) : tr(STR_SLEEP_MOVE_TO_PAUSE);
  // Blank rather than "Set sleep cover" when the file cannot become one: a .png is
  // only usable as a sleep image while the Transparent face is selected.
  const auto labels = triage ? mappedInput.mapLabels(tr(STR_BACK), favLabel, tr(STR_DELETE), pauseLabel)
                             : mappedInput.mapLabels(tr(STR_BACK), canSetSleepCover() ? tr(STR_SET_SLEEP_COVER) : "",
                                                     (hasPrevious ? "<" : ""), (hasNext ? ">" : ""));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

bool BmpViewerActivity::canSetSleepCover() const {
  return FsHelpers::hasBmpExtension(filePath) ||
         (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM &&
          FsHelpers::hasPngExtension(filePath));
}

bool BmpViewerActivity::renderPng() {
  ImageDimensions dimensions;
  if (!PngToFramebufferConverter::getDimensionsStatic(filePath, dimensions)) return false;
  if (dimensions.width <= 0 || dimensions.height <= 0) return false;

  const float scale = std::min(static_cast<float>(renderer.getScreenWidth()) / dimensions.width,
                               static_cast<float>(renderer.getScreenHeight()) / dimensions.height);
  const int width = std::min(renderer.getScreenWidth(), static_cast<int>(dimensions.width * std::min(scale, 1.0f)));
  const int height = std::min(renderer.getScreenHeight(), static_cast<int>(dimensions.height * std::min(scale, 1.0f)));
  RenderConfig config{(renderer.getScreenWidth() - width) / 2, (renderer.getScreenHeight() - height) / 2, width,
                      height};

  PngToFramebufferConverter converter;
  return converter.decodeToFramebuffer(filePath, renderer, config);
}

void BmpViewerActivity::onEnter() {
  Activity::onEnter();

  if (siblingImages.empty() && !filePath.empty()) {
    loadSiblingImages();
  }

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  Rect popupRect = GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  GUI.fillPopupProgress(renderer, popupRect, 20);  // Initial 20% progress
  if (FsHelpers::hasPngExtension(filePath)) {
    renderer.clearScreen();
    if (renderPng()) {
      // drawHints(), not a private label set: a .png sitting in a sleep folder must
      // offer the same favourite/pause/delete triage a .bmp or .pxc does there.
      drawHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
    return;
  }

  HalFile file;
  // 1. Open the BMP file
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

      GUI.fillPopupProgress(renderer, popupRect, 50);

      renderer.clearScreen();
      // Assuming drawBitmap defaults to 0,0 crop if omitted, or pass explicitly: drawBitmap(bitmap, x, y, pageWidth,
      // pageHeight, 0, 0)
      renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, 0, 0);

      // Draw UI hints on the base layer
      drawHints();
      // Single pass for non-grayscale images

      renderer.displayBuffer(HalDisplay::FAST_REFRESH);

    } else {
      // Handle file parsing error
      renderer.clearScreen();
      renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_INVALID_BMP_FILE));
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }

    file.close();
  } else {
    // Handle file open error
    renderer.clearScreen();
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_FILE_OPEN_FAILED));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void BmpViewerActivity::onExit() {
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void BmpViewerActivity::doSetSleepCover() {
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));

  // While the sleep face is Transparent, "set as sleep image" means "use this as the
  // overlay" — copying to /sleep.bmp would write a file that face never reads, and
  // switching the mode to CUSTOM behind the user's back would drop them out of it.
  const bool transparentMode = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
  if (!canSetSleepCover()) return;

  const char* destination =
      transparentMode ? (FsHelpers::hasPngExtension(filePath) ? TRANSPARENT_SLEEP_ROOT_PNG : TRANSPARENT_SLEEP_ROOT_BMP)
                      : CUSTOM_SLEEP_ROOT_BMP;

  // Already the destination: nothing to copy, and opening it for write would truncate
  // the very file being read.
  bool success = filePath == destination;
  if (!success) {
    HalFile inFile, outFile;
    if (Storage.openFileForRead("BMP", filePath, inFile)) {
      if (Storage.openFileForWrite("BMP", destination, outFile)) {
        char buffer[2048];
        int bytesRead;
        success = true;
        while ((bytesRead = inFile.read(buffer, sizeof(buffer))) > 0) {
          if (outFile.write(buffer, bytesRead) != bytesRead) {
            success = false;
            break;
          }
        }
        if (bytesRead < 0) success = false;
        outFile.close();
      }
      inFile.close();
    }
  }

  if (success) {
    if (!transparentMode) SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    SETTINGS.saveToFile();
    GUI.drawPopup(renderer, tr(STR_DONE));
  } else {
    GUI.drawPopup(renderer, tr(STR_FAILED_LOWER));
  }

  delay(1000);
  onEnter();
}

void BmpViewerActivity::loop() {
  // Keep CPU awake/polling so 1st click works
  Activity::loop();

  auto openSibling = [this](const int delta) {
    if (currentImageIndex < 0) {
      return false;
    }
    const int nextIndex = currentImageIndex + delta;
    if (siblingImages.size() <= 1 || nextIndex < 0 || nextIndex >= static_cast<int>(siblingImages.size())) {
      return false;
    }
    currentImageIndex = nextIndex;
    std::string dirPath = FsHelpers::extractFolderPath(filePath);
    if (dirPath.back() != '/') dirPath += "/";
    filePath = dirPath + std::string(siblingImages[currentImageIndex]);
    onEnter();
    return true;
  };

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToFileBrowser(filePath);
    return;
  }

  const bool triage = crosspoint::sleep::isUnderSleepDirs(filePath);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (triage) {
      doToggleFavorite();
    } else if (canSetSleepCover()) {
      doSetSleepCover();
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Left)) {
    if (triage) {
      promptDelete();
    } else {
      openSibling(-1);
    }
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Right)) {
    if (triage) {
      doTogglePause();
    } else {
      openSibling(1);
    }
    return;
  }

  // Siblings stay on Up/Down whatever the folder, so stepping through a wallpaper
  // folder is the same movement in both viewers.
  if (mappedInput.wasReleased(MappedInputManager::Button::Up)) {
    openSibling(-1);
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Down)) {
    openSibling(1);
    return;
  }
}

void BmpViewerActivity::doToggleFavorite() {
  const bool makeFavorite = !FavoriteImage::isFavoritePath(filePath);
  std::string newPath;
  switch (FavoriteImage::setFavorite(filePath, makeFavorite, &newPath)) {
    case FavoriteImage::SetFavoriteResult::Success:
      // The file was renamed, so the viewer follows it and the sibling list is stale.
      // Only the name moved: the image on screen is identical, and the one thing that
      // has to change is the Fav/Unfav word. Re-entering would re-open, re-parse and
      // re-decode the whole BMP behind a Loading popup to repaint the same pixels, so
      // repaint the hint strip over the framebuffer that is already holding the image
      // and refresh differentially instead.
      //
      // The sibling list is rebuilt rather than dropped: openSibling() does not reload
      // an empty list, it just stops working.
      filePath = newPath;
      loadSiblingImages();
      drawHints();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      return;
    case FavoriteImage::SetFavoriteResult::RenameConflict:
      GUI.drawPopup(renderer, tr(STR_FAVORITE_NAME_EXISTS));
      break;
    default:
      GUI.drawPopup(renderer, tr(STR_FAVORITE_FAILED));
      break;
  }
  delay(1000);
  onEnter();
}

void BmpViewerActivity::doTogglePause() {
  // Pick the neighbour before the move, while this file still anchors the lookup.
  const int nextIndex = (currentImageIndex > 0) ? currentImageIndex - 1 : currentImageIndex + 1;
  const bool hasNeighbour =
      siblingImages.size() > 1 && nextIndex >= 0 && nextIndex < static_cast<int>(siblingImages.size());
  std::string nextName = hasNeighbour ? std::string(siblingImages[nextIndex]) : std::string();

  if (!crosspoint::sleep::toggleSleepPause(filePath).ok) {
    GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
    delay(1000);
    onEnter();
    return;
  }

  // The file left this folder. Show the neighbour rather than dumping the user back
  // to the browser after every move, so triage keeps flowing.
  if (nextName.empty()) {
    activityManager.goToFileBrowser(FsHelpers::extractFolderPath(filePath));
    return;
  }
  std::string dirPath = FsHelpers::extractFolderPath(filePath);
  if (dirPath.back() != '/') dirPath += "/";
  filePath = dirPath + nextName;
  siblingImages.clear();
  onEnter();
}

void BmpViewerActivity::promptDelete() {
  const std::string doomed = filePath;
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_DELETE) + std::string("? "),
                                             FavoriteImage::displayNameForPath(doomed)),
      [this, doomed](const ActivityResult& res) {
        if (res.isCancelled) {
          onEnter();
          return;
        }
        if (!Storage.remove(doomed.c_str())) {
          GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
          delay(1000);
          onEnter();
          return;
        }
        // The wake path re-renders the last wallpaper; a dead path
        // there sends the next wake to the boot logo for no reason.
        FavoriteImage::removePathReferences(doomed);
        activityManager.goToFileBrowser(FsHelpers::extractFolderPath(doomed));
      });
}
