#include "SleepImageTriage.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SleepPauseToggle.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImage.h"

namespace SleepImageTriage {

namespace {
void reportFailure(GfxRenderer& renderer, const char* message) {
  GUI.drawPopup(renderer, message);
  delay(1000);
}
}  // namespace

int lineHeightForHelp(const GfxRenderer& renderer) { return renderer.getLineHeight(UI_10_FONT_ID); }

std::string effectivePath(const std::string& path) {
  const std::string queued = DeferredFavorite::pendingTargetFor(path);
  return queued.empty() ? path : queued;
}

bool effectiveFavorite(const std::string& path) { return FavoriteImage::isFavoritePath(effectivePath(path)); }

bool toggleFavorite(GfxRenderer& renderer, const std::string& path) {
  const std::string current = effectivePath(path);
  const bool makeFavorite = !FavoriteImage::isFavoritePath(current);
  const std::string target = FavoriteImage::favoritePathFor(current, makeFavorite);
  if (target.empty() || target == current) return true;
  if (DeferredFavorite::request(current, target)) {
    FavoriteImage::replacePathReferences(current, target);
    return true;
  }
  // Queue jammed. Do it in the foreground rather than drop the press, and report a
  // failure rather than closing as though the press had worked.
  if (FavoriteImage::setFavorite(current, makeFavorite, nullptr) == FavoriteImage::SetFavoriteResult::Success) {
    return true;
  }
  reportFailure(renderer, tr(STR_FAVORITE_FAILED));
  return false;
}

std::string drainFavorites(const std::string& path) {
  const std::string queued = DeferredFavorite::pendingTargetFor(path);
  DeferredFavorite::waitForIdle(15000);
  DeferredFavorite::reconcile();
  return (!queued.empty() && Storage.exists(queued.c_str())) ? queued : path;
}

bool deleteImage(GfxRenderer& renderer, std::string& path) {
  path = drainFavorites(path);
  // Measured while the file still exists: an on-device delete then costs the index
  // one dead slot instead of a folder walk at the next unlock.
  const auto pendingDelete = crosspoint::sleep::windex::planDeletion(path);
  if (!Storage.remove(path.c_str())) {
    LOG_ERR("TRIAGE", "Failed to delete: %s", path.c_str());
    reportFailure(renderer, tr(STR_DELETE_FAILED));
    return false;
  }
  crosspoint::sleep::windex::commitDeletion(pendingDelete);
  // The wake path re-renders the last wallpaper; a dead path there sends the next
  // wake to the boot logo for no reason.
  FavoriteImage::removePathReferences(path);
  return true;
}

bool togglePause(GfxRenderer& renderer, const std::string& path) {
  if (crosspoint::sleep::toggleSleepPause(path).ok) return true;
  reportFailure(renderer, tr(STR_MOVE_FAILED));
  return false;
}

}  // namespace SleepImageTriage
