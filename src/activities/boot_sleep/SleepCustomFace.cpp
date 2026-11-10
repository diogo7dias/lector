// SleepActivity's Custom face: a wallpaper from the card, picked from /sleep (or the
// root sleep.pxc / sleep.bmp), falling back to the cover and then the plain face.
// Split out of SleepActivity.cpp to keep each file under the token limit.
#include <Bitmap.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "SleepActivity.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "dev/LockLab.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperPick.h"

namespace {

// The waveform of a .pxc sleep face drawn in 1 bit, which only happens on a panel without
// gray (DeviceProfile::panelHasGray). HALF: the same clean pass sleep_face::planFor gives
// a face flattened to black and white.
constexpr HalDisplay::RefreshMode kPxcOneBitRefresh = HalDisplay::HALF_REFRESH;

}  // namespace

// One wallpaper file, .pxc or .bmp, onto the panel. Returns false when the file cannot be
// opened or parsed, which is also how a name that the index still lists but the folder no
// longer holds is discovered: the pick does not probe for liveness (a probe costs a full
// directory lookup, 1271 ms measured on a real wallpaper folder) and the open here
// already answers the same question.
bool SleepActivity::renderWallpaperFile(const std::string& filename, const PxcRenderOptions* const pxcOptions,
                                        const uint32_t linePosition, const uint32_t lineTotal) const {
  LOG_INF("SLP", "Loading wallpaper: %s", filename.c_str());
  const SleepInfoOverlayScope overlayScope(filename, linePosition, lineTotal);
  if (hasPxcExtension(filename)) {
    if (renderPxcSleepScreen(renderer, filename, display.profile().panelHasGray, kPxcOneBitRefresh,
                             &drawSleepInfoOverlay, pxcOptions)) {
      APP_STATE.lastSleepWallpaperPath = filename;
      return true;
    }
    return false;
  }
  // Storage.open rather than openFileForRead: the latter calls exists() before
  // open(), which is a second full directory lookup for the same name and measured
  // as half of a 2543 ms wallpaper open. A failed open is reported here instead.
  HalFile file = Storage.open(filename.c_str(), O_RDONLY);
  if (!file) {
    LOG_INF("SLP", "wallpaper open failed: %s", filename.c_str());
    return false;
  }
  // Stretch the tone range only when the user has asked for no cover filter, so a
  // filtered image still looks the way they set it. .bmp only; a .pxc is already
  // quantised to four levels when the file is written.
  const bool adaptiveTone = SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
  Bitmap bitmap(file, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
  if (bitmap.parseHeaders() != BmpReaderError::Ok) return false;
  renderBitmapSleepScreen(bitmap, sleep_face::Face::Wallpaper);
  APP_STATE.lastSleepWallpaperPath = filename;
  return true;
}

// Refresh Wallpaper: the picture the last lock left, drawn again over itself. No popup,
// no pre-clear and no new pick: the panel already holds this image, the second drive is
// what clears the traces the first one left.
bool SleepActivity::repaintWallpaper(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& path) {
  const SleepActivity painter(renderer, mappedInput);
  display.setInverted(false);
  prepareSleepReadingLine();
  const bool painted = painter.renderWallpaperFile(path, nullptr, 0, 0);
  clearSleepReadingLine();
  return painted;
}

void SleepActivity::renderCustomSleepScreen() const {
#ifdef LECTOR_LOCK_LAB
  // A Full lock run has to draw what the bench draws, or the number it reports belongs to
  // a different recipe than the picture on the panel.
  const PxcRenderOptions labOptions = locklab::optionsFor(APP_STATE.lockLab);
  const PxcRenderOptions* const pxcOptions = &labOptions;
#else
  const PxcRenderOptions* const pxcOptions = nullptr;
#endif
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

  // /sleep.pxc (Lector Wallpaper Converter format), same root-priority tier as
  // /sleep.bmp. renderPxcSleepScreen opens the path itself and returns false when
  // the file is absent/invalid, so we just try it and fall through on failure.
  // Both devices render .pxc through the OEM 3-pass grayscale pipeline, matching the
  // BMP wallpaper and cover sleep screens. The X4 previously stalled here: its HALF
  // sequence powers the panel rails down, and the grayscale refresh that follows then
  // shared one activation with the rail ramp. Fixed at the driver-config level; see
  // src/platform/LectorSsd1677Config.cpp.
  // The 3-pass grayscale wherever the panel can draw it. This used to be a setting, and it
  // was the wrong thing to offer: the sleep screen is the picture the device wears while
  // it is off, so tone is the whole point of it, and the second or so the extra two panel
  // refreshes cost is spent after the user has already put the device down.
  //
  // A panel without gray (UC8279 LUT_VER 0x67, DeviceProfile::panelHasGray) gets the
  // dithered 1-bit pass instead, one HALF refresh: the grayscale path there would drop
  // both planes in the SDK and leave the two mid levels thresholded to flat black/white.
  //
  // On the grayscale path the panel does sleep_face::planFor(Face::Wallpaper,
  // /*sourceHasGrayscale=*/true, ...): a base at that plan's waveform, then the two
  // grayscale planes. Two submissions on top of the popup.
  const bool pxcGrayscale = display.profile().panelHasGray;
  {
    const SleepInfoOverlayScope overlayScope("/sleep.pxc");
    if (renderPxcSleepScreen(renderer, "/sleep.pxc", pxcGrayscale, kPxcOneBitRefresh, &drawSleepInfoOverlay,
                             pxcOptions)) {
      LOG_INF("SLP", "Loaded: /sleep.pxc");
      APP_STATE.lastSleepWallpaperPath = "/sleep.pxc";
      return;
    }
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
      return renderWallpaperFile(std::string(sleepDir) + "/" + name, pxcOptions, linePosition, lineTotal);
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
  // never comes back here, so the chain cannot loop.
  renderCoverSleepScreen();
}
