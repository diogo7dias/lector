#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "util/TaskWatchdog.h"

namespace {

// A FAT directory is a flat array of fixed-size slots, so a random wallpaper can be
// reached by SEEKING to a random slot rather than walking every entry. SdFat's
// openNext() reads from the directory's current position, which makes the jump legal:
// it returns the first live entry at or after wherever the position was left.
//
// Cost is ~2*log2(entries) probes (roughly 25 reads for a 4000-file folder) instead of
// one read per file. The old full walk took seconds on a large folder and could wedge
// sleep entry outright.
//
// Fairness note: a file consumes one slot per 13 characters of long name, plus one, so
// long-named files cover more slots and are picked slightly more often. Files named to
// fit 8.3 (e.g. "A3F9.PXC") occupy exactly one slot each, which makes the pick exactly
// uniform. scripts/rename_wallpapers.py renames a folder into that form.
constexpr size_t DIR_SLOT_BYTES = 32;
// 131072 slots — far past any real wallpaper folder, and only a bound for the doubling
// probe, not an allocation.
constexpr size_t MAX_DIR_BYTES = 4u * 1024u * 1024u;
// After landing, how many slots to walk looking for a wallpaper before giving up. A
// wallpaper folder is essentially all wallpapers, so this normally resolves in one step.
constexpr int MAX_FORWARD_SLOTS = 512;

bool isWallpaperName(const char* name) {
  if (name[0] == '\0' || name[0] == '.') return false;
  return hasPxcExtension(name) || FsHelpers::hasBmpExtension(name);
}

// True when at least one live directory entry exists at or after `offset`. Monotone in
// `offset` (entries are contiguous and terminated by a free slot), which is what makes
// the binary search below valid.
bool entryExistsAt(HalFile& dir, const size_t offset) {
  if (!dir.seekSet(offset)) return false;
  auto probe = dir.openNextFile();
  const bool found = static_cast<bool>(probe);
  if (probe) probe.close();
  return found;
}

// Random wallpaper name, or empty when the jump could not resolve one (caller falls
// back to the full walk). `dir` is left at an arbitrary position.
std::string pickWallpaperByJump(HalFile& dir) {
  if (!entryExistsAt(dir, 0)) return {};

  // Grow a bound until a probe lands past the last entry.
  size_t lastLive = 0;
  size_t past = DIR_SLOT_BYTES;
  while (past < MAX_DIR_BYTES && entryExistsAt(dir, past)) {
    lastLive = past;
    past *= 2;
  }
  // Narrow to the last slot that still yields an entry.
  while (past - lastLive > DIR_SLOT_BYTES) {
    const size_t mid = ((lastLive + (past - lastLive) / 2) / DIR_SLOT_BYTES) * DIR_SLOT_BYTES;
    if (mid == lastLive) break;
    if (entryExistsAt(dir, mid)) {
      lastLive = mid;
    } else {
      past = mid;
    }
  }

  const uint32_t slots = static_cast<uint32_t>(lastLive / DIR_SLOT_BYTES) + 1;
  size_t start = static_cast<size_t>(random(static_cast<long>(slots))) * DIR_SLOT_BYTES;

  char name[256];  // FAT long-file-name maximum (255 characters plus terminator)
  // Two passes: the landing slot may sit in a trailing run of non-wallpapers, in which
  // case the walk runs off the end and restarts from the top.
  for (int pass = 0; pass < 2; pass++) {
    if (!dir.seekSet(start)) break;
    for (int step = 0; step < MAX_FORWARD_SLOTS; step++) {
      auto entry = dir.openNextFile();
      if (!entry) break;  // end of directory — wrap
      const bool isDir = entry.isDirectory();
      entry.getName(name, sizeof(name));
      entry.close();
      if (!isDir && isWallpaperName(name)) return std::string(name);
    }
    start = 0;
  }
  return {};
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  // Deep sleep is a chip reset, so the wake cannot know what the panel is holding unless
  // we write it down. Clear first and let the render path set it, so any screen that is
  // not a wallpaper leaves it empty. Saved only when it changed: a fixed /sleep.pxc gives
  // the same value every sleep and must not cost an SD write each time.
  const std::string previousWallpaper = APP_STATE.lastSleepWallpaperPath;
  APP_STATE.lastSleepWallpaperPath.clear();

  renderSleepScreen();

  if (APP_STATE.lastSleepWallpaperPath != previousWallpaper) {
    APP_STATE.saveToFile();
  }
}

void SleepActivity::renderSleepScreen() const {
  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Show popup with reader orientation only when going to sleep from reader
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }

  deepCleanPanel();

  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
      return renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      if (APP_STATE.lastSleepFromReader) {
        return renderCoverSleepScreen();
      } else {
        return renderCustomSleepScreen();
      }
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderCustomSleepScreen() const {
  // Check if we have a /.sleep (preferred) or /sleep directory
  const char* sleepDir = nullptr;
  auto dir = Storage.open("/.sleep");

  // Look for sleep.bmp on the root of the sd card to determine if we should
  // render a custom sleep screen instead of the default.
  // This takes priority over the /sleep folder.
  HalFile file;
  if (Storage.openFileForRead("SLP", "/sleep.bmp", file)) {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Loading: /sleep.bmp");
      renderBitmapSleepScreen(bitmap);
      APP_STATE.lastSleepWallpaperPath = "/sleep.bmp";
      file.close();
      if (dir) dir.close();
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
  constexpr bool pxcGrayscale = true;
  if (renderPxcSleepScreen(renderer, "/sleep.pxc", pxcGrayscale)) {
    LOG_INF("SLP", "Loaded: /sleep.pxc");
    APP_STATE.lastSleepWallpaperPath = "/sleep.pxc";
    if (dir) dir.close();
    return;
  }

  if (dir && dir.isDirectory()) {
    sleepDir = "/.sleep";
  } else {
    dir = Storage.open("/sleep");
    if (dir && dir.isDirectory()) {
      sleepDir = "/sleep";
    }
  }

  if (sleepDir) {
    // Fast path: seek straight to a random directory slot (see pickWallpaperByJump).
    const uint32_t pickStartMs = millis();
    std::string chosen = pickWallpaperByJump(dir);
    if (!chosen.empty()) {
      LOG_INF("SLP", "jump pick in %ums", static_cast<unsigned>(millis() - pickStartMs));
    }

    // Fallback: walk the whole folder with reservoir sampling — keep the k-th valid
    // file with probability 1/k, so every file is equally likely, using O(1) memory
    // and no per-file header read. Only reached when the jump cannot resolve a name.
    uint32_t seen = 0;
    uint32_t scanned = 0;
    const uint32_t scanStartMs = millis();
    char name[256];  // FAT long-file-name maximum (255 chars + terminator)
    dir.rewindDirectory();
    for (auto dirFile = chosen.empty() ? dir.openNextFile() : HalFile(); dirFile; dirFile = dir.openNextFile()) {
      // A wallpaper folder can hold thousands of files, and this walk runs inline on the
      // loop task while going to sleep. Without a periodic yield the task never returns
      // to the scheduler, the task watchdog has no chance to be fed, and sleep entry can
      // wedge hard enough to need a physical reset.
      if ((++scanned & 0x3F) == 0) {
        resetTaskWatchdogIfSubscribed();
        vTaskDelay(1);
      }
      const bool isDir = dirFile.isDirectory();
      dirFile.getName(name, sizeof(name));
      dirFile.close();  // only the name is needed; never open/parse the file here
      if (isDir || !isWallpaperName(name)) continue;
      ++seen;
      if (random(static_cast<long>(seen)) == 0) chosen = name;
    }
    if (scanned > 0) {
      LOG_INF("SLP", "fallback scan: %u entries (%u wallpapers) in %ums", scanned, seen,
              static_cast<unsigned>(millis() - scanStartMs));
    }
    if (!chosen.empty()) {
      const auto filename = std::string(sleepDir) + "/" + chosen;
      LOG_INF("SLP", "Randomly loading: %s", filename.c_str());
      delay(100);
      if (hasPxcExtension(chosen)) {
        if (renderPxcSleepScreen(renderer, filename, pxcGrayscale)) {
          APP_STATE.lastSleepWallpaperPath = filename;
          dir.close();
          return;
        }
      } else {
        HalFile randFile;
        if (Storage.openFileForRead("SLP", filename, randFile)) {
          Bitmap bitmap(randFile, true);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            renderBitmapSleepScreen(bitmap);
            APP_STATE.lastSleepWallpaperPath = filename;
            randFile.close();
            dir.close();
            return;
          }
          randFile.close();
        }
      }
    }
  }
  if (dir) dir.close();

  renderDefaultSleepScreen();
}

// Locking can happen over any screen, and every sleep face below paints with a
// differential waveform: HALF for the logo/blank/bitmap faces, and the calibrated
// graybase + gray-nudge LUT for the grayscale wallpaper pipeline. A differential
// only transitions the pixels that changed, so whatever was on the panel stays
// visible underneath the wallpaper for the whole sleep (device photo 2026-07-25:
// the Text Settings menu reading through a .pxc face). The "Entering sleep" popup
// does not provide the clean either — it ends in a FAST_REFRESH, which is also
// differential.
//
// One blank FULL pass fixes it. FULL selects the multi-flash GC waveform that
// upstream deliberately avoids for sleep (#2471's blinking complaint), and it
// costs roughly 1.5 s, but at this point sleep is already committed: there is no
// page turn to delay and the flashing is hidden behind the lock.
//
// Not called for the quick-resume face, which must keep the frame it inherits.
void SleepActivity::deepCleanPanel() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::FULL_REFRESH);
}

// Sleep screens paint with a single HALF refresh (stock parity): the OEM X4
// firmware's only clean refresh in normal operation is the single-pass 0xD7
// sequence, used once for the sleep image. It never runs the multi-flash GC
// waveform (0xF7) that FULL_REFRESH selects (#2471's blinking complaint).
void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::REGULAR);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap) const {
  int x, y;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  float cropX = 0, cropY = 0;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  if (bitmap.getWidth() > pageWidth || bitmap.getHeight() > pageHeight) {
    // image will scale, make sure placement is right
    float ratio = static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    LOG_DBG("SLP", "bitmap ratio: %f, screen ratio: %f", ratio, screenRatio);
    if (ratio > screenRatio) {
      // image wider than viewport ratio, scaled down image needs to be centered vertically
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropX = 1.0f - (screenRatio / ratio);
        LOG_DBG("SLP", "Cropping bitmap x: %f", cropX);
        ratio = (1.0f - cropX) * static_cast<float>(bitmap.getWidth()) / static_cast<float>(bitmap.getHeight());
      }
      x = 0;
      y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
      LOG_DBG("SLP", "Centering with ratio %f to y=%d", ratio, y);
    } else {
      // image taller than viewport ratio, scaled down image needs to be centered horizontally
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        cropY = 1.0f - (ratio / screenRatio);
        LOG_DBG("SLP", "Cropping bitmap y: %f", cropY);
        ratio = static_cast<float>(bitmap.getWidth()) / ((1.0f - cropY) * static_cast<float>(bitmap.getHeight()));
      }
      x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      y = 0;
      LOG_DBG("SLP", "Centering with ratio %f to x=%d", ratio, x);
    }
  } else {
    // center the image
    x = (pageWidth - bitmap.getWidth()) / 2;
    y = (pageHeight - bitmap.getHeight()) / 2;
  }

  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  const bool hasGreyscale = bitmap.hasGreyscale() &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  if (hasGreyscale) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    renderer.copyGrayscaleMsbBuffers();

    renderer.displayGrayBuffer();
    renderer.setRenderMode(GfxRenderer::BW);
  }
}

void SleepActivity::renderCoverSleepScreen() const {
  void (SleepActivity::*renderNoCoverSleepScreen)() const;
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      renderNoCoverSleepScreen = &SleepActivity::renderCustomSleepScreen;
      break;
    default:
      renderNoCoverSleepScreen = &SleepActivity::renderDefaultSleepScreen;
      break;
  }

  if (APP_STATE.openEpubPath.empty()) {
    return (this->*renderNoCoverSleepScreen)();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return (this->*renderNoCoverSleepScreen)();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return (this->*renderNoCoverSleepScreen)();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return (this->*renderNoCoverSleepScreen)();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap);
      return;
    }
  }

  return (this->*renderNoCoverSleepScreen)();
}

void SleepActivity::renderLastScreenSleepScreen() const {
  const auto pageHeight = renderer.getScreenHeight();
  renderer.drawImage(MoonIcon, 0, pageHeight - MOONICON_HEIGHT, MOONICON_WIDTH, MOONICON_HEIGHT);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
