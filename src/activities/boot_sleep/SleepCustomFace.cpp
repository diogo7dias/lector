// SleepActivity's Custom face: a wallpaper from the card, picked from /sleep (or the
// root /sleep.bmp), falling back only to the plain face when no valid wallpaper exists.
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
#include "sleep/SleepIndexReconcilePolicy.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperPick.h"
#include "util/FavoriteImageNames.h"

// One .bmp wallpaper file onto the panel. Returns false when the file cannot be
// opened or parsed, which is also how a name that the index still lists but the folder no
// longer holds is discovered: the pick does not probe for liveness (a probe costs a full
// directory lookup, 1271 ms measured on a real wallpaper folder) and the open here
// already answers the same question.
bool SleepActivity::renderWallpaperFile(const std::string& filename, const uint32_t linePosition,
                                        const uint32_t lineTotal, bool* const missing) const {
  LOG_INF("SLP", "Loading wallpaper: %s", filename.c_str());
  const SleepInfoOverlayScope overlayScope(filename, linePosition, lineTotal);
  // Storage.open rather than openFileForRead: the latter calls exists() before
  // open(), which is a second full directory lookup for the same name and measured
  // as half of a 2543 ms wallpaper open. A failed open is reported here instead.
  HalFile file = Storage.open(filename.c_str(), O_RDONLY);
  if (missing != nullptr) *missing = !file;
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

void SleepActivity::renderCustomSleepScreen() const {
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
    const auto renderName = [&](const std::string& name, const uint32_t pos, const uint32_t total,
                                bool* missing = nullptr) {
      return renderWallpaperFile(std::string(sleepDir) + "/" + name, pos, total, missing);
    };

    std::string triedAlt;
    if (!chosen.empty()) {
      bool missing = false;
      if (renderName(chosen, linePosition, lineTotal, &missing)) {
        dir.close();
        return;
      }
      // A favorite toggle renames x.bmp <-> x_F.bmp without rewriting the index record.
      // Try the counterpart before declaring the record stale.
      triedAlt = FavoriteImage::favoriteCounterpart(chosen);
      if (!triedAlt.empty() && triedAlt != chosen) {
        bool altMissing = false;
        if (renderName(triedAlt, linePosition, lineTotal, &altMissing)) {
          dir.close();
          return;
        }
        missing = missing && altMissing;
      }
      if (pickedFromIndex) {
        const bool rebuild = sleep_reconcile::renderFailureNeedsRebuild(missing, APP_STATE.sleepIndexDeadSlots);
        if (rebuild) APP_STATE.sleepIndexNeedsRebuild = true;
        LOG_INF("SLP", "index record %s, %s", missing ? "missing" : "unreadable",
                rebuild ? "index rebuild next wake" : "skipped");
      }
      // Retry a few O(log N) jump picks before falling back to a linear scan.
      constexpr int MAX_FALLBACK_JUMPS = 4;
      for (int attempt = 0; attempt < MAX_FALLBACK_JUMPS; attempt++) {
        const std::string again = crosspoint::sleep::pickWallpaperByJump(dir);
        if (again.empty() || again == chosen || again == triedAlt) continue;
        if (renderName(again, 0, 0)) {
          dir.close();
          return;
        }
      }
    }

    // Last resort: scan the directory from a random slot until any valid wallpaper renders.
    struct ScanCtx {
      const SleepActivity* self;
      const char* sleepDir;
      const std::string* skipA;
      const std::string* skipB;
    } ctx{this, sleepDir, &chosen, &triedAlt};
    const auto tryScanName = [](const char* name, void* rawCtx) -> bool {
      const auto* c = static_cast<const ScanCtx*>(rawCtx);
      if (!c->skipA->empty() && *c->skipA == name) return false;
      if (!c->skipB->empty() && *c->skipB == name) return false;
      return c->self->renderWallpaperFile(std::string(c->sleepDir) + "/" + name, 0, 0);
    };
    if (crosspoint::sleep::pickWallpaperByScan(dir, tryScanName, &ctx)) {
      dir.close();
      return;
    }
  }
  if (dir) dir.close();

  // Match CrossPoint: Custom mode never falls back to the book cover. Only when no valid
  // wallpaper exists at all does it show the default face.
  renderDefaultSleepScreen();
}
