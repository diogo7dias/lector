#include "PxcViewerActivity.h"

#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <utility>

#include "ConfirmationActivity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "components/BusyBanner.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SdSleepImageFs.h"
#include "sleep/SleepPauseToggle.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperNeighbour.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImage.h"

namespace {
// The one line an error state gets, in the theme's own help face: these screens
// are an image and nothing else, so the message is all the chrome they have.
int lineHeightForHelp(const GfxRenderer& renderer) { return renderer.getLineHeight(SMALL_FONT_ID); }
}  // namespace


namespace {

// renderPxcSleepScreen's overlay hook is a bare function pointer with no context
// parameter, so the live viewer reaches it through here. Only one viewer exists
// at a time (activities are a stack and this one blocks while it is on top), and
// it is cleared in onExit, so the pointer cannot outlive the activity.
const PxcViewerActivity* g_activeViewer = nullptr;

void drawHintsTrampoline(GfxRenderer&) {
  if (g_activeViewer != nullptr) g_activeViewer->drawHintsForOverlay();
}

std::string folderOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string("/") : path.substr(0, slash);
}

std::string baseNameOf(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

PxcViewerActivity::PxcViewerActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string filePath)
    : Activity("PxcViewer", renderer, mappedInput), filePath(std::move(filePath)) {}

std::string PxcViewerActivity::effectivePath() const {
  const std::string queued = DeferredFavorite::pendingTargetFor(filePath);
  return queued.empty() ? filePath : queued;
}

bool PxcViewerActivity::effectiveFavorite() const { return FavoriteImage::isFavoritePath(effectivePath()); }

void PxcViewerActivity::drawHintsForOverlay() const { drawHints(); }

void PxcViewerActivity::drawHints() const {
  const char* favLabel = effectiveFavorite() ? tr(STR_UNFAV) : tr(STR_FAV);
  // "Pause" only means something for a wallpaper inside a rotation folder. For
  // anything else the slot stays blank rather than offering a hidden no-op.
  const char* pauseLabel = "";
  if (crosspoint::sleep::isUnderSleepDirs(filePath)) {
    pauseLabel = filePath.rfind("/sleep pause/", 0) == 0 ? tr(STR_SLEEP_MOVE_TO_SLEEP) : tr(STR_SLEEP_MOVE_TO_PAUSE);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), favLabel, tr(STR_DELETE), pauseLabel);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

// Favouriting renames the file on the card. It does not change one pixel of the
// wallpaper — only the Fav/Unfav word in the hint strip. Re-running render() to flip
// that word costs a blank FULL pass, a re-read and re-decode of the whole .pxc, and a
// HALF refresh: roughly four seconds of panel time on the X3 to repaint an identical
// image. Repainting the strip and refreshing differentially is one FAST pass.
//
// Safe only because the 1-bit pxc path leaves the finished image in the framebuffer:
// renderPxcSleepScreen decodes into it, draws the overlay, then refreshes once, and
// nothing clears it afterwards. Do NOT reuse this after the grayscale path, which
// clears the buffer once per plane and leaves it holding the MSB plane, not the image.
void PxcViewerActivity::refreshHintsOnly() const {
  drawHints();
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

void PxcViewerActivity::render() {
  // Deep clean first. The viewer paints straight over the file-browser list, and
  // every wallpaper refresh below is differential, so without a blank FULL pass
  // the list ghosts through the image (same failure as the sleep screen).
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);

  // 1-bit, not the 3-pass grayscale the lock screen uses. Two reasons: the single
  // pass is roughly a third of the panel time, which is what makes stepping
  // through a folder of hundreds of images bearable; and the overlay hook fires
  // once, so the button hints composite into the same refresh as the wallpaper
  // rather than costing a second one. Tone is what the lock screen is for.
  g_activeViewer = this;
  const bool ok =
      renderPxcSleepScreen(renderer, filePath, /*grayscale=*/false, HalDisplay::HALF_REFRESH, &drawHintsTrampoline);
  g_activeViewer = nullptr;
  if (ok) return;

  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHelpText(renderer, Rect{0, pageHeight / 2, renderer.getScreenWidth(), lineHeightForHelp(renderer)},
                   tr(STR_PXC_WRONG_SIZE));
  drawHints();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void PxcViewerActivity::onEnter() {
  Activity::onEnter();
  render();
}

void PxcViewerActivity::onExit() {
  g_activeViewer = nullptr;
  Activity::onExit();
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void PxcViewerActivity::openNeighbour(const int delta) {
  // Each step scans the folder once, so on a card with thousands of wallpapers
  // the gap between button and image is real. The banner fills it.
  BusyBanner banner(renderer, tr(STR_BUSY_READING_WALLPAPERS));
  crosspoint::sleep::SdSleepImageFs fs;
  const std::string folder = folderOf(filePath);
  const std::string next =
      crosspoint::sleep::neighbourWallpaper(fs, folder.c_str(), baseNameOf(filePath), /*forward=*/delta > 0);
  if (next.empty()) return;  // already at that end of the folder
  filePath = (folder == "/" ? "" : folder) + "/" + next;
  render();
}

void PxcViewerActivity::loop() {
  Activity::loop();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    // Hand the browser the file this viewer was showing so it reopens on that
    // row. After a delete the file is gone, so fall back to the folder.
    activityManager.goToFileBrowser(fileStillPresent ? filePath : folderOf(filePath));
    return;
  }

  // Confirm favourites, because it is the action this screen exists for and the
  // one that gets used most while triaging a folder.
  //
  // The rename does NOT happen here. On a FAT card every name-based operation is a
  // linear scan of the directory, and /sleep holds thousands of wallpapers, so doing it
  // on the press left the user watching the viewer for seconds before it would move. The
  // job is queued in RAM and the viewer closes straight back to the browser, so triaging
  // a folder is press, back, next, at button speed. DeferredFavorite drains the queue
  // when the browser is left or the device is locked, moments that already do card work.
  //
  // The browser is handed the CURRENT path, not the favorited one: the card still holds
  // the old name until the queue drains, and that is the row the browser has to find. It
  // draws the queued name through DeferredFavorite::pendingTargetFor, so the row reads as
  // favorited even though the rename has not run yet.
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    const bool makeFavorite = !effectiveFavorite();
    const std::string target = FavoriteImage::favoritePathFor(effectivePath(), makeFavorite);
    if (!target.empty() && target != effectivePath()) {
      if (DeferredFavorite::request(effectivePath(), target)) {
        FavoriteImage::replacePathReferences(effectivePath(), target);
      } else {
        // Queue jammed. Do it in the foreground rather than drop the press, the way this
        // screen always did.
        if (FavoriteImage::setFavorite(filePath, makeFavorite, nullptr) != FavoriteImage::SetFavoriteResult::Success) {
          GUI.drawPopup(renderer, tr(STR_FAVORITE_FAILED));
          delay(1000);
          render();
          return;
        }
      }
    }
    activityManager.goToFileBrowser(filePath);
    return;
  }

  // Left deletes, behind a confirmation. On success the browser reopens at this
  // file's folder; on cancel or failure the viewer re-renders.
  if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
    startActivityForResult(std::make_unique<ConfirmationActivity>(
                               renderer, mappedInput, tr(STR_DELETE) + std::string("? "), baseNameOf(filePath)),
                           [this](const ActivityResult& res) {
                             if (res.isCancelled) {
                               render();
                               return;
                             }
                             // A queued favorite rename would move this file out from under
                             // the delete, so let it land first. Card work is expected here
                             // anyway, and the user has already confirmed.
                             //
                             // The name is captured BEFORE the drain and adopted after it.
                             // Draining is what performs the rename, so filePath is exactly
                             // the name that stops existing; deleting it afterwards fails and
                             // the user's confirmed delete is silently refused.
                             const std::string doomed = effectivePath();
                             DeferredFavorite::waitForIdle(15000);
                             DeferredFavorite::reconcile();
                             filePath = Storage.exists(doomed.c_str()) ? doomed : filePath;
                             // Measured while the file still exists: an on-device delete then
                             // costs the index one dead slot instead of a folder walk.
                             const auto pendingDelete = crosspoint::sleep::windex::planDeletion(filePath);
                             if (!Storage.remove(filePath.c_str())) {
                               LOG_ERR("PXC", "Failed to delete: %s", filePath.c_str());
                               render();
                               return;
                             }
                             crosspoint::sleep::windex::commitDeletion(pendingDelete);
                             // The wake path re-renders this file to composite the unlock banners
                             // over it; leaving a dead path there means the next wake falls back to
                             // the boot logo for no reason.
                             FavoriteImage::removePathReferences(filePath);
                             fileStillPresent = false;
                             activityManager.goToFileBrowser(folderOf(filePath));
                           });
    return;
  }

  // Right moves the wallpaper between /sleep and "/sleep pause".
  if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
    if (!crosspoint::sleep::isUnderSleepDirs(filePath)) return;

    // Same reason as the delete above: the move and a queued rename are both name-based
    // operations on this one file, so the queue drains before the move is worked out, and
    // the post-drain name is adopted before anything is worked out from it.
    const std::string queuedName = effectivePath();
    DeferredFavorite::waitForIdle(15000);
    DeferredFavorite::reconcile();
    if (Storage.exists(queuedName.c_str())) filePath = queuedName;

    // Work out where to go next BEFORE the move, while the file is still in place
    // to anchor the neighbour lookup. Falling back to the previous entry keeps the
    // last wallpaper in a folder from dead-ending the flow.
    const std::string folder = folderOf(filePath);
    crosspoint::sleep::SdSleepImageFs fs;
    const std::string current = baseNameOf(filePath);
    std::string nextName = crosspoint::sleep::neighbourWallpaper(fs, folder.c_str(), current, /*forward=*/true);
    if (nextName.empty()) {
      nextName = crosspoint::sleep::neighbourWallpaper(fs, folder.c_str(), current, /*forward=*/false);
    }

    const auto moved = crosspoint::sleep::toggleSleepPause(filePath);
    if (!moved.ok) {
      GUI.drawPopup(renderer, tr(STR_MOVE_FAILED));
      delay(1000);
      render();
      return;
    }
    // The file left this folder, so show the neighbour instead of dumping the
    // user back to the browser after every move — triage keeps flowing.
    if (nextName.empty()) {
      activityManager.goToFileBrowser(folder);
      return;
    }
    filePath = folder + "/" + nextName;
    render();
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Up)) {
    openNeighbour(-1);
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Down)) {
    openNeighbour(1);
    return;
  }
}
