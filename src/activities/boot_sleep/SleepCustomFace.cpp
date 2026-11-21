// SleepActivity's Custom face: a wallpaper from the card, picked from /sleep (or the
// root /sleep.bmp), falling back to the cover and then the plain face.
// Split out of SleepActivity.cpp to keep each file under the token limit.
#include <Bitmap.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepActivity.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperPick.h"

// One .bmp wallpaper file onto the panel. Returns false when the file cannot be
// opened or parsed, which is also how a name that the index still lists but the folder no
// longer holds is discovered: the pick does not probe for liveness (a probe costs a full
// directory lookup, 1271 ms measured on a real wallpaper folder) and the open here
// already answers the same question.
bool SleepActivity::renderWallpaperFile(const std::string& filename, const uint32_t linePosition,
                                        const uint32_t lineTotal) const {
  LOG_INF("SLP", "Loading wallpaper: %s", filename.c_str());
  const SleepInfoOverlayScope overlayScope(filename, linePosition, lineTotal);
  // Storage.open rather than openFileForRead: the latter calls exists() before
  // open(), which is a second full directory lookup for the same name and measured
  // as half of a 2543 ms wallpaper open. A failed open is reported here instead.
  HalFile file = Storage.open(filename.c_str(), O_RDONLY);
  if (!file) {
    LOG_INF("SLP", "wallpaper open failed: %s", filename.c_str());
    return false;
  }
  // Stretch the tone range only when the user has asked for no cover filter, so a
  // filtered image still looks the way they set it.
  const bool adaptiveTone = SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  Bitmap bitmap(file, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  renderBitmapSleepScreen(bitmap, sleep_face::Face::Wallpaper);
  APP_STATE.lastSleepWallpaperPath = filename;
  return true;
}

void SleepActivity::renderCustomSleepScreen(const bool thenCover) const {
  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    const bool adaptiveTone =
        SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
    Bitmap bitmap(file, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      const SleepInfoOverlayScope overlayScope("/sleep.bmp");
      renderBitmapSleepScreen(bitmap, sleep_face::Face::Wallpaper);
      APP_STATE.lastSleepWallpaperPath = "/sleep.bmp";
      file.close();
      return;
    }
    file.close();
  }

  // Which wallpaper folder to rotate over. Resolved through the index store so
  // the pick and the index can never disagree about it: /sleep wins, /.sleep is
  // the fallback for cards written by older firmware. Asked only here, below the
  // root-file tier above, which outranks the folder either way.
  const char* const sleepDir = crosspoint::sleep::windex::dirPathForId(crosspoint::sleep::windex::resolveSleepDirId());
  auto dir = Storage.open(sleepDir);

  if (dir && dir.isDirectory()) {
    const crosspoint::sleep::WallpaperPick pick = crosspoint::sleep::pickWallpaper(dir, sleepDir, previousWallpaper);
    const std::string& chosen = pick.chosen;
    const bool pickedFromIndex = pick.pickedFromIndex;
    const uint32_t linePosition = pick.linePosition;
    const uint32_t lineTotal = pick.lineTotal;
    const auto renderChosen = [&](const std::string& name) {
      return renderWallpaperFile(std::string(sleepDir) + "/" + name, linePosition, lineTotal);
    };

    if (!chosen.empty()) {
      if (renderChosen(chosen)) {
        dir.close();
        return;
      }
      // The index named a file that is no longer renderable. Flag the index for rebuild
      // on the next cold boot and fall back to the jump pick for tonight, rather than
      // dropping the user to the logo face over one stale record.
      if (pickedFromIndex) {
        APP_STATE.sleepIndexNeedsRebuild = true;
        LOG_INF("SLP", "index record unrenderable, falling back to jump pick");
        const std::string again = crosspoint::sleep::pickWallpaperByJump(dir);
        if (!again.empty() && renderChosen(again)) {
          dir.close();
          return;
        }
      }
    }
  }
  if (dir) dir.close();

  // No wallpaper to show: the open book's cover, then the Lector fallback. The cover path
  // comes back here only from Cover + Custom, which passes thenCover=false, so the chain
  // cannot loop.
  if (thenCover) return renderCoverSleepScreen();
  renderDefaultSleepScreen();
}
