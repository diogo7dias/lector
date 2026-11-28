#include "SleepActivity.h"

#include <ClockZones.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_random.h>

#include <algorithm>
#include <cmath>
#include <ctime>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "LectorFace.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "SleepOverlay.h"
#include "SleepPreClear.h"
#include "SleepTiming.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/WakeFacePolicy.h"
#include "util/ClippingsReader.h"
#include "util/DeferredFavorite.h"
#include "util/QuoteText.h"

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

// The faces that draw on the page the reader left instead of replacing it.
bool keepsPage(const uint8_t mode) {
  return mode == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
         mode == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM;
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  // Sleep screens always use normal polarity. This activity draws directly
  // from onEnter (outside ActivityManager's per-render polarity resolution),
  // so clear any inversion left over from a night-mode reader render. Every face
  // repaints the whole panel, so dropping the inversion costs it nothing.
  //
  // Quick Resume and Transparent keep the page and draw on it, so night mode's output
  // inversion is made real in the framebuffer first: the page looks the same, and what the
  // face adds (and the frame Quick Resume saves) is in normal polarity.
  const bool frameWasInverted = display.isInverted();
  display.setInverted(false);
  const bool quickResume =
      wake_face::usesQuickResumeForSleep(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen, fromTimeout);
  if (frameWasInverted && (quickResume || keepsPage(SETTINGS.sleepScreen))) renderer.invertScreen();

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

  renderSleepScreen();
  SleepTiming::mark("face");
}

void SleepActivity::renderSleepScreen() const {
  // Quick Resume asks nothing of the card and keeps the page: no popup, no scrub.
  if (wake_face::usesQuickResumeForSleep(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen, fromTimeout)) {
    return renderQuickResumeScreen();
  }

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
  // Transparent draws over the page, so nothing may scrub or replace it: the popup is put up
  // and taken back out of the framebuffer, and there is no pre-clear. The overlay's own
  // waveform is the only pass that cleans, as CrossPoint does.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::TRANSPARENT_CUSTOM) {
    showPopupKeepingPage();
    SleepTiming::mark("popup");
    return renderTransparentCustomSleepScreen();
  }

  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }
  SleepTiming::mark("popup");

  // Match CrossPoint: every mode below replaces the whole screen, so clean to white once
  // with HALF_REFRESH first (BLANK skips this and runs that single white HALF_REFRESH itself).
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::BLANK) {
    const uint32_t preClearMs = sleepPreClear(renderer, display.profile());
    LOG_INF("SLP", "pre-clear %ums", static_cast<unsigned>(preClearMs));
  }

  // Custom: wallpaper, then the Lector fallback (never the book cover). Cover: cover, then
  // the fallback. Quick Resume and Transparent keep the page on the panel above; a value no
  // build offers is migrated to Custom on load, and the default keeps a stale one from a
  // blank panel.
  switch (SETTINGS.sleepScreen) {
    case (CrossPointSettings::SLEEP_SCREEN_MODE::DARK):
      return renderDefaultSleepScreen(/*dark=*/true);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT):
      return renderDefaultSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::BLANK):
      return renderBlankSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER):
      return renderCoverSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_TRANSPARENT):
      // The cover with the overlay over it; no cover means the overlay over a blank page.
      return renderCoverSleepScreen(CoverFallback::Overlay);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM):
      // The cover while reading, a wallpaper otherwise (and when the book has no cover).
      return APP_STATE.lastSleepFromReader ? renderCoverSleepScreen(CoverFallback::Custom) : renderCustomSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::QUOTE):
      return renderQuoteSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM):
    default:
      return renderCustomSleepScreen();
  }
}

// The Lector fallback: a white page with the crow and the name centred. Reached
// only when no wallpaper and no cover could be shown (no files, no open book, a decode
// that failed partway). Every face that fails lands here.
//
// The stock-parity rule this face obeys now lives in sleep_face::planFor next to the two
// other faces, so all three read as one table instead of three literals in three
// functions.
void SleepActivity::renderDefaultSleepScreen(const bool dark) const {
  renderer.clearScreen();
  drawLectorFace(renderer);
  // Dark is the same face on a black page. The fallback and Light stay white.
  if (dark) renderer.invertScreen();
  const sleep_face::PaintPlan plan =
      sleep_face::planFor(sleep_face::Face::PlainLector, /*sourceHasGrayscale=*/false, display.profile());
  renderer.displayBuffer(plan.base);
}

// None: an empty white page, one HALF_REFRESH (pre-clear was skipped above).
void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderQuoteSleepScreen() const {
  clippings::Record rec;
  bool found = false;
  {
    HalFile file;
    if (Storage.openFileForRead("SLP", quote_text::CLIPPINGS_PATH, file)) {
      const uint64_t fileSize = file.fileSize64();
      if (fileSize > 0) {
        uint64_t seed = 0;
        uint16_t year = 0;
        uint8_t month = 0, day = 0, hour = 0, minute = 0;
        if (halClock.getDateTime(year, month, day, hour, minute)) {
          const int64_t utc = clock_zones::utcSeconds(year, month, day, hour, minute);
          const time_t local = static_cast<time_t>(
              utc + clock_zones::offsetMinutes(SETTINGS.clockTimezone, SETTINGS.clockDst, utc) * 60);
          tm t{};
          gmtime_r(&local, &t);
          seed = clippings::dailySeed(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        } else {
          seed = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
        }
        auto readChunk = [](void* ctx, const uint64_t offset, char* dst, const size_t len) -> size_t {
          auto* f = static_cast<HalFile*>(ctx);
          if (!f->seek64(offset)) return 0;
          const int n = f->read(dst, len);
          return n > 0 ? static_cast<size_t>(n) : 0;
        };
        found = clippings::pickAtOffset(fileSize, clippings::offsetForSeed(seed, fileSize), readChunk, &file, rec);
      }
    }
  }

  if (!found) {
    if (!APP_STATE.openEpubPath.empty()) return renderCoverSleepScreen();
    return renderDefaultSleepScreen();
  }

  // Collapse internal line breaks into single spaces so wrappedText typesets cleanly.
  std::string body = rec.quote;
  for (char& c : body) {
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  }

  int fontId = SETTINGS.getReaderFontId();
  if (renderer.getLineHeight(fontId) <= 0) fontId = CHAREINK_14_FONT_ID;

  renderer.prewarmFallbackText(fontId, body.c_str());
  renderer.prewarmFallbackText(UI_10_FONT_ID, rec.title.c_str());
  if (!rec.author.empty()) renderer.prewarmFallbackText(UI_10_FONT_ID, rec.author.c_str());

  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int padX = std::max(36, pageW / 10);
  const int contentX = viewLeft + padX;
  const int contentW = std::max(80, pageW - viewLeft - viewRight - padX * 2);
  const int usableTop = viewTop + 32;
  const int usableH = std::max(120, pageH - viewTop - viewBottom - 64);

  const int quoteLineH = std::max(16, renderer.getLineHeight(fontId) + 4);
  const int attrLineH = std::max(14, renderer.getLineHeight(UI_10_FONT_ID) + 2);
  constexpr int kRuleH = 2;
  constexpr int kRuleGap = 24;
  constexpr int kAttrGap = 18;

  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, rec.title.c_str(), contentW, 2, EpdFontFamily::BOLD);
  const auto authorLines = rec.author.empty() ? std::vector<std::string>{}
                                              : renderer.wrappedText(UI_10_FONT_ID, rec.author.c_str(), contentW, 1);

  const int chromeH = kRuleH * 2 + kRuleGap * 2 + kAttrGap + static_cast<int>(titleLines.size()) * attrLineH +
                      static_cast<int>(authorLines.size()) * attrLineH;
  const int maxQuoteLines = std::max(1, (usableH - chromeH) / quoteLineH);
  const auto quoteLines = renderer.wrappedText(fontId, body.c_str(), contentW, maxQuoteLines);

  const int blockH = chromeH + static_cast<int>(quoteLines.size()) * quoteLineH;
  int y = usableTop + std::max(0, (usableH - blockH) / 2);

  renderer.clearScreen();
  const int ruleW = std::min(contentW, std::max(96, contentW / 2));
  const int ruleX = contentX + (contentW - ruleW) / 2;
  renderer.fillRect(ruleX, y, ruleW, kRuleH, true);
  y += kRuleH + kRuleGap;

  for (const auto& line : quoteLines) {
    renderer.drawText(fontId, contentX, y, line.c_str(), true);
    y += quoteLineH;
  }

  y += kRuleGap;
  renderer.fillRect(ruleX, y, ruleW, kRuleH, true);
  y += kRuleH + kAttrGap;

  const Rect area{contentX, usableTop, contentW, usableH};
  for (const auto& line : titleLines) {
    UITheme::drawCenteredText(renderer, area, UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += attrLineH;
  }
  for (const auto& line : authorLines) {
    UITheme::drawCenteredText(renderer, area, UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::REGULAR);
    y += attrLineH;
  }

  drawSleepInfoOverlay(renderer);
  SleepTiming::mark("decode");
  const sleep_face::PaintPlan plan =
      sleep_face::planFor(sleep_face::Face::PlainLector, /*sourceHasGrayscale=*/false, display.profile());
  renderer.displayBuffer(plan.base);
}

void SleepActivity::renderBitmapSleepScreen(Bitmap& bitmap, const sleep_face::Face face,
                                            SleepOverlay* const overlay) const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  // Fit shrinks while decoding, so the dither lands on panel pixels; the renderer would
  // otherwise drop pixels from a source-size dither. CROP still scales in the renderer.
  if (SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::FIT) {
    bitmap.ditherToFit(pageWidth, pageHeight);
  }
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
  // An overlay can carry grey of its own, so it asks for the planes too.
  const bool hasGreyscale = (bitmap.hasGreyscale() || overlay) && display.profile().panelHasGray &&
                            SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;

  // The face's whole panel recipe in one value: which waveform the base runs at, how many
  // submissions this costs, whether the two grayscale planes follow. Replaces the literal
  // that used to sit in each branch below. sourceHasGrayscale is what the image offers AND
  // the cover filter allows, which is the same question hasGreyscale answered before.
  const sleep_face::PaintPlan plan = sleep_face::planFor(face, hasGreyscale, display.profile());
  // The planes draw the picture twice more; on the X4 Pro the rows decoded for the base
  // are kept in PSRAM and replayed. Elsewhere, or out of PSRAM, each pass decodes again.
  if (plan.grayscalePlanes) bitmap.cacheRows();

  renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);

  if (SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::INVERTED_BLACK_AND_WHITE) {
    renderer.invertScreen();
  }

  // Drawn after the filter so the label is never inverted along with the image,
  // and once per pass below for the same reason the bitmap is. On the cover face only
  // the reading line draws: the wallpaper badges need a SleepInfoOverlayScope.
  drawSleepInfoOverlay(renderer);
  if (overlay && !overlay->draw(renderer)) LOG_ERR("SLP", "Overlay failed on the cover");

  // Everything above this point is card reading and decoding; everything below is the
  // panel. The gap between "decode" and "face" is the wallpaper's own refresh cost.
  SleepTiming::mark("decode");

  if (plan.grayscalePlanes) {
    // OEM grayscale pipeline base. Must stay HALF: the gray nudge LUT is
    // calibrated against the pixel state the single-pass HALF waveform leaves
    // behind. A FULL (GC) base parks pixels in a different charge state and
    // the differential nudge then lands unevenly (blotchy noise in gray areas).
    //
    // Settled for the X4 Pro on hardware: FULL collapsed the wallpaper's mid tones to a
    // single grey, so every device takes HALF (sleepGrayscaleBaseRefresh).
    renderer.displayGrayscaleBase(plan.base);
    SleepTiming::mark("base");
  } else {
    renderer.displayBuffer(plan.base);
  }

  if (plan.grayscalePlanes) {
    // The rewind is a file seek on the bitmap (a cache replay when cacheRows held); it used
    // to run before the plane's clear and mode switch, which touch only the framebuffer, so
    // the order between them is moot.
    sleep_face::paintGrayscalePlanes(
        renderer,
        [&] {
          bitmap.rewindToData();
          renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
          drawSleepInfoOverlay(renderer);
          return !overlay || overlay->draw(renderer);
        },
        [](GfxRenderer::RenderMode) { SleepTiming::mark("plane"); });
  }
}

// What the cover face shows when there is no book, no cover, or the cover cannot be read:
// the Lector face (Cover), the wallpaper (Cover + Custom; Custom's own cover fallback is off
// there, so the two cannot send each other round in a circle), or the overlay on its own
// (Cover + Transparent).
void SleepActivity::renderNoCoverSleepScreen(const CoverFallback fallback) const {
  if (fallback == CoverFallback::Custom) return renderCustomSleepScreen();
  if (fallback == CoverFallback::Overlay) return renderTransparentCustomSleepScreen();
  renderDefaultSleepScreen();
}

// The open book's cover, then `fallback`.
void SleepActivity::renderCoverSleepScreen(const CoverFallback fallback) const {
  if (APP_STATE.openEpubPath.empty()) {
    return renderNoCoverSleepScreen(fallback);
  }

  std::string coverBmpPath;
  bool cropped = SETTINGS.sleepScreenCoverMode == CrossPointSettings::SLEEP_SCREEN_COVER_MODE::CROP;

  // Check if the current book is XTC, TXT, or EPUB
  if (FsHelpers::hasXtcExtension(APP_STATE.openEpubPath)) {
    Xtc lastXtc(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastXtc.load()) {
      LOG_ERR("SLP", "Failed to load last XTC");
      return renderNoCoverSleepScreen(fallback);
    }

    if (!lastXtc.generateCoverBmp()) {
      LOG_ERR("SLP", "Failed to generate XTC cover bmp");
      return renderNoCoverSleepScreen(fallback);
    }

    coverBmpPath = lastXtc.getCoverBmpPath();
  } else if (FsHelpers::hasTextBookExtension(APP_STATE.openEpubPath)) {
    // Handle TXT file - looks for cover image in the same folder
    Txt lastTxt(APP_STATE.openEpubPath, "/.crosspoint");
    if (!lastTxt.load()) {
      LOG_ERR("SLP", "Failed to load last TXT");
      return renderNoCoverSleepScreen(fallback);
    }

    if (!lastTxt.generateCoverBmp()) {
      LOG_ERR("SLP", "No cover image found for TXT file");
      return renderNoCoverSleepScreen(fallback);
    }

    coverBmpPath = lastTxt.getCoverBmpPath();
  } else if (FsHelpers::hasEpubExtension(APP_STATE.openEpubPath)) {
    Epub lastEpub(APP_STATE.openEpubPath, "/.crosspoint");
    // Skip loading css since we only need metadata here
    if (!lastEpub.load(true, true)) {
      LOG_ERR("SLP", "Failed to load last epub");
      return renderNoCoverSleepScreen(fallback);
    }

    if (!lastEpub.generateCoverBmp(cropped)) {
      LOG_ERR("SLP", "Failed to generate cover bmp");
      return renderNoCoverSleepScreen(fallback);
    }

    coverBmpPath = lastEpub.getCoverBmpPath(cropped);
  } else {
    return renderNoCoverSleepScreen(fallback);
  }

  HalFile file;
  if (Storage.openFileForRead("SLP", coverBmpPath, file)) {
    Bitmap bitmap(file);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      LOG_DBG("SLP", "Rendering sleep cover: %s", coverBmpPath.c_str());
      // Cover + Transparent: no usable overlay just means the plain cover.
      SleepOverlay overlay;
      const bool withOverlay = fallback == CoverFallback::Overlay && overlay.prepare(renderer);
      renderBitmapSleepScreen(bitmap, sleep_face::Face::CoverFallback, withOverlay ? &overlay : nullptr);
      return;
    }
  }

  return renderNoCoverSleepScreen(fallback);
}
