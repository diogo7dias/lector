#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

#include <cmath>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "SleepPreClear.h"
#include "SleepTiming.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/DeferredFavorite.h"

namespace {
struct BitmapPlacement {
  int x = 0;
  int y = 0;
  float cropX = 0.0f;
  float cropY = 0.0f;
};

// Where a bitmap lands on the panel: centred, scaled down to fit, and cropped to
// fill when the user picked CROP.
BitmapPlacement calculateBitmapPlacement(const int bitmapWidth, const int bitmapHeight, const GfxRenderer& renderer) {
  BitmapPlacement placement;
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  if (bitmapWidth > pageWidth || bitmapHeight > pageHeight) {
    float ratio = static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
    const float screenRatio = static_cast<float>(pageWidth) / static_cast<float>(pageHeight);

    if (ratio > screenRatio) {
      // Wider than the viewport: centre the scaled image vertically.
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropX = 1.0f - (screenRatio / ratio);
        ratio = (1.0f - placement.cropX) * static_cast<float>(bitmapWidth) / static_cast<float>(bitmapHeight);
      }
      placement.x = 0;
      placement.y = std::round((static_cast<float>(pageHeight) - static_cast<float>(pageWidth) / ratio) / 2);
    } else {
      // Taller than the viewport: centre the scaled image horizontally.
      if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP) {
        placement.cropY = 1.0f - (ratio / screenRatio);
        ratio = static_cast<float>(bitmapWidth) / ((1.0f - placement.cropY) * static_cast<float>(bitmapHeight));
      }
      placement.x = std::round((static_cast<float>(pageWidth) - static_cast<float>(pageHeight) * ratio) / 2);
      placement.y = 0;
    }
  } else {
    placement.x = (pageWidth - bitmapWidth) / 2;
    placement.y = (pageHeight - bitmapHeight) / 2;
  }

  return placement;
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render. Every face
  // repaints the whole panel, so dropping the inversion costs it nothing.
  display.setInverted(false);

  // Run and land any queued favourite rename before the folder is touched and before the
  // state below is written. The renames DELIBERATELY start here, not at the press: on a
  // FAT card every name operation is a linear directory scan — seconds with thousands of
  // wallpapers — and each scan holds the storage mutex against the reader's page turns.
  // The lock is the moment the user has stopped reading, so the seconds belong here.
  // Deep sleep is a chip reset: a rename still sitting in the queue would be lost, while
  // the name it was promised could already have been saved to the card. Bounded so a
  // jammed worker can never block sleeping; sized for a few scan-heavy renames.
  if (!DeferredFavorite::waitForIdle(15000)) DeferredFavorite::abandonUnstarted(30000);
  DeferredFavorite::reconcile();
  SleepTiming::mark("favs");

  // Deep sleep is a chip reset, so the wake cannot know what the panel is holding unless
  // we write it down. Clear first and let the render path set it, so any screen that is
  // not a wallpaper leaves it empty.
  // ponytail: no save here. enterDeepSleep() writes APP_STATE once after this returns,
  // so a second write of the same state only slowed the lock.
  previousWallpaper = APP_STATE.lastSleepWallpaperPath;
  APP_STATE.lastSleepWallpaperPath.clear();

  prepareSleepReadingLine();
  renderSleepScreen();
  clearSleepReadingLine();
  SleepTiming::mark("face");
}

void SleepActivity::renderSleepScreen() const {
  // The "Entering sleep" popup, in the reader's orientation when locking from a book.
  // Both faces read the card and can take seconds, so the press gets a visible answer
  // first.
  //
  // This popup is one panel submission (sleep_face::POPUP_SUBMISSIONS); the face that
  // follows costs one or two more depending on which face it is and whether its source
  // carries tone — see sleep_face::planFor, whose rows test/sleep_face_paint asserts. The
  // comment that used to sit here said "every lock is two panel submissions", which had
  // been false for the grayscale faces for as long as they have existed: those cost
  // three (popup, BW base, the two grayscale planes committed together).
  //
  // POSSIBLE SAVING, hardware-gated — do not take it from a code reading. drawPopup ends
  // in a full FAST submission (617 ms measured at the low end), and it runs BEFORE the
  // face is chosen, so the cheapest lock drives the panel twice to show one picture. The
  // paint/submit split now exists to make composing them possible: GUI.drawBannerStrip()
  // paints without submitting, so the popup could ride into the face's own submission.
  // Not done, for two reasons that need a device to settle: removing a submission is
  // physically observable (and this one is the user's visible answer to a press that then
  // spends seconds reading the card), and the face's own base is HALF/FULL, not FAST, so
  // the composed pass would put the popup on a different waveform than it runs today.
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }
  SleepTiming::mark("popup");

  // Before anything is drawn, and before any sleep face is chosen (Cover mode included): a scrub is about the
  // panel's charge history, not about which picture is going on top of it.
  //
  // This is the lock screen's only defence against a ghost. No face's own paint clears the
  // panel: every board writes the picture against a baseline that leaves the pixels which
  // are NOT changing sitting on whatever charge the reader left them with. On the X3 the
  // grayscale base's requestResync() was mistaken for a clear; it is a sync, and it flashes
  // only the pure-white part of the frame. See SleepPreClear.h for the waveform cells.
  //
  // The cost is a clean pass at lock, which the user does not wait for: it runs after the
  // device has already been put down. One submission on the X3, four elsewhere.
  const uint32_t preClearMs = sleepPreClear(renderer, display.profile());
  LOG_INF("SLP", "pre-clear %ums", static_cast<unsigned>(preClearMs));

  // Custom: wallpaper, then cover, then the Lector fallback. Cover: cover, then the
  // fallback. Retired values are migrated to Custom when settings load, so nothing else
  // can arrive here; the default keeps a stale value from a blank panel.
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
    default:
      return renderCustomSleepScreen();
  }
}

// The Lector fallback: a white page with the name centred, in the one UI face. Reached
// only when no wallpaper and no cover could be shown (no files, no open book, a decode
// that failed partway). Every face that fails lands here.
//
// The stock-parity rule this face obeys now lives in sleep_face::planFor next to the two
// other faces, so all three read as one table instead of three literals in three
// functions.
void SleepActivity::renderDefaultSleepScreen() const {
  renderer.clearScreen();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  renderer.drawCenteredText(UI_10_FONT_ID, (renderer.getScreenHeight() - lineHeight) / 2, tr(STR_LECTOR));
  const sleep_face::PaintPlan plan =
      sleep_face::planFor(sleep_face::Face::PlainLector, /*sourceHasGrayscale=*/false, display.profile());
  renderer.displayBuffer(plan.base);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const sleep_face::Face face) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto placement = calculateBitmapPlacement(bitmap.getWidth(), bitmap.getHeight(), renderer);
  const int x = placement.x;
  const int y = placement.y;
  const float cropX = placement.cropX;
  const float cropY = placement.cropY;

  LOG_DBG("SLP", "bitmap %d x %d, screen %d x %d", bitmap.getWidth(), bitmap.getHeight(), pageWidth, pageHeight);
  LOG_DBG("SLP", "drawing to %d x %d", x, y);
  renderer.clearScreen();

  // The cover filter describes how a full-screen image should look: NO_FILTER keeps the
  // tone (grayscale), the others flatten it to black and white.
  // A panel without gray (DeviceProfile::panelHasGray) skips the planes it cannot draw.
  const bool hasGreyscale = bitmap.hasGreyscale() && display.profile().panelHasGray &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Drawn after the filter so the label is never inverted along with the image,
  // and once per pass below for the same reason the bitmap is. On the cover face only
  // the reading line draws: the wallpaper badges need a SleepInfoOverlayScope.
  drawSleepInfoOverlay(renderer);

  // Everything above this point is card reading and decoding; everything below is the
  // panel. The gap between "decode" and "face" is the wallpaper's own refresh cost.
  SleepTiming::mark("decode");

  // The face's whole panel recipe in one value: which waveform the base runs at, how many
  // submissions this costs, whether the two grayscale planes follow. Replaces the literal
  // that used to sit in each branch below. sourceHasGrayscale is what the image offers AND
  // the cover filter allows, which is the same question hasGreyscale answered before.
  const sleep_face::PaintPlan plan = sleep_face::planFor(face, hasGreyscale, display.profile());

  if (plan.grayscalePlanes) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    //
    // Settled for the X4 Pro on hardware: FULL collapsed the wallpaper's mid tones to a
    // single grey, so every device takes HALF (sleepGrayscaleBaseRefresh).
    renderer.displayGrayscaleBase(plan.base);
  } else {
    renderer.displayBuffer(plan.base);
  }

  if (plan.grayscalePlanes) {
    // The rewind is a file seek on the bitmap; it used to run before the plane's clear and
    // mode switch, which touch only the framebuffer, so the order between them is moot.
    sleep_face::paintGrayscalePlanes(
        renderer,
        [&] {
          bitmap.rewindToData();
          renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
          drawSleepInfoOverlay(renderer);
          return true;
        },
        [](GfxRenderer::RenderMode) {});
  }
}

// The open book's cover, or the Lector fallback when there is no book, no cover, or the
// cover cannot be read.
void SleepActivity::renderCoverSleepScreen() const {
  if (APP_STATE.openEpubPath.empty()) {
    return renderDefaultSleepScreen();
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    // Handle XTC file
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return renderDefaultSleepScreen();
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTxtExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return renderDefaultSleepScreen();
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    // Handle EPUB file
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return renderDefaultSleepScreen();
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return renderDefaultSleepScreen();
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return renderDefaultSleepScreen();
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      renderBitmapSleepScreen(bitmap, sleep_face::Face::CoverFallback);
      return;
    }
  }

  return renderDefaultSleepScreen();
}
