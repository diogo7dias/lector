#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_random.h>
#include <strings.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "RecentBooksStore.h"
#include "SleepInfoOverlay.h"
#include "StatsDashboardPolicy.h"
#include "StatsDashboardRenderer.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/BootLogos.h"
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsStore.h"
#include "reading_stats/SdStatsFiles.h"
#include "sleep/Wallpaper.h"

static_assert(CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD == stats_dashboard::kStatsDashboardMode);
static_assert(CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD_PLUS == stats_dashboard::kStatsDashboardPlusMode);

namespace {

std::string fileNameFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

RecentBook recentBookForPath(const std::string& path) {
  const auto& books = RECENT_BOOKS.getBooks();
  const auto found =
      std::find_if(books.begin(), books.end(), [&](const RecentBook& book) { return book.path == path; });
  return found == books.end() ? RecentBook{path, fileNameFromPath(path), "", ""} : *found;
}

}  // namespace

void SleepActivity::onEnter() {
  Activity::onEnter();

  const bool renderQuickResume =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);

  if (renderQuickResume) {
    return renderLastScreenSleepScreen();
  }

  // Custom-wallpaper path (both quality modes): skip the "Entering sleep"
  // banner — its own HALF_REFRESH (~1.7 s) only shows a transient popup that
  // the wallpaper paints over anyway. Fast mode lands the image in ~2 s;
  // Pretty mode takes ~6 s but sleep is already committed at this point (no
  // page-turn risk), so the frozen page reads as "locking" without a banner.
  // Slow paths (covers, dashboards) keep the banner as progress feedback.
  const bool routesToCustom =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM && !APP_STATE.lastSleepFromReader) ||
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM_LOGO_CUSTOM &&
       APP_STATE.lastSleepFromReader);
  const bool directWallpaperLock = routesToCustom && SETTINGS.wallpaperFormat == CrossPointSettings::WALLPAPER_PXC;

  // Show popup with reader orientation only when going to sleep from reader
  if (!directWallpaperLock) {
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
  }

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
    case (CrossPointSettings::SLEEP_SCREEN_MODE::UNTIL_DEATH):
      return renderUntilDeathSleepScreen();
    case (CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM_LOGO_CUSTOM):
      // Slept from a book -> custom wallpaper rotation; from anywhere else -> random logo.
      if (APP_STATE.lastSleepFromReader) {
        return renderCustomSleepScreen();
      } else {
        return renderUntilDeathSleepScreen();
      }
    case (CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD):
      return renderStatsDashboardSleepScreen(false);
    case (CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD_PLUS):
      return renderStatsDashboardSleepScreen(true);
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderStatsDashboardSleepScreen(const bool wallpaper) const {
  const std::string& path = APP_STATE.openEpubPath;
  if (path.empty() || !stats_dashboard::supportsBook(path)) {
    return renderDefaultSleepScreen();
  }

  RecentBook recent = recentBookForPath(path);
  stats_dashboard::DashboardData data;
  data.title = recent.title;
  data.progressPercent = static_cast<uint8_t>(std::clamp(recent.progressPercent, 0, 100));
  std::string cachePath;
  std::string coverPath;
  constexpr int kDashboardCoverHeight = 444;

  if (FsHelpers::hasEpubExtension(path)) {
    Epub epub(path, "/.crosspoint");
    cachePath = epub.getCachePath();
    const bool loaded = epub.load(false, true);
    if (data.title.empty() && loaded) data.title = epub.getTitle();
    coverPath = UITheme::getCoverThumbPath(recent.coverBmpPath, kDashboardCoverHeight);

    if (loaded) {
      HalFile progressFile;
      if (Storage.openFileForRead("DASH", cachePath + "/progress.bin", progressFile)) {
        uint8_t progress[6] = {};
        const int count = progressFile.read(progress, sizeof(progress));
        progressFile.close();
        if (count >= 4) {
          const int spineIndex = progress[0] | (progress[1] << 8);
          const int tocIndex = epub.getTocIndexForSpineIndex(spineIndex);
          if (tocIndex >= 0) data.chapter = epub.getTocItem(tocIndex).title;
        }
      }
    }
  } else if (FsHelpers::hasXtcExtension(path)) {
    Xtc xtc(path, "/.crosspoint");
    cachePath = xtc.getCachePath();
    const bool loaded = xtc.load();
    if (data.title.empty() && loaded) data.title = xtc.getTitle();
    coverPath = UITheme::getCoverThumbPath(recent.coverBmpPath, kDashboardCoverHeight);

    if (loaded) {
      HalFile progressFile;
      if (Storage.openFileForRead("DASH", cachePath + "/progress.bin", progressFile)) {
        uint8_t progress[4] = {};
        const int count = progressFile.read(progress, sizeof(progress));
        progressFile.close();
        if (count == 4) {
          const uint32_t page = progress[0] | (progress[1] << 8) | (progress[2] << 16) | (progress[3] << 24);
          data.progressPercent = xtc.calculateProgress(page);
          const auto& chapters = xtc.getChapters();
          const auto chapter = std::find_if(chapters.begin(), chapters.end(), [&](const xtc::ChapterInfo& item) {
            return page >= item.startPage && page <= item.endPage;
          });
          if (chapter != chapters.end()) data.chapter = chapter->name;
        }
      }
    }
  } else {
    Txt txt(path, "/.crosspoint");
    cachePath = txt.getCachePath();
    const bool loaded = txt.load();
    if (data.title.empty() && loaded) data.title = txt.getTitle();
    if (loaded && Storage.exists(txt.getCoverBmpPath().c_str())) coverPath = txt.getCoverBmpPath();
  }

  if (data.title.empty()) data.title = fileNameFromPath(path);

  const auto now = reading_stats::currentLocalDateTime();
  data.todayDay = now.valid ? now.dayIndex : 0;
  reading_stats::SdStatsFiles files;
  reading_stats::ReadingStatsStore statsStore(files);
  statsStore.load(cachePath + "/reading_stats.bin", data.book);
  statsStore.load(reading_stats::ReaderStatsSession::globalPath(), data.global);
  if (data.book.resetEpoch != data.global.resetEpoch) {
    data.book = {};
    data.book.resetEpoch = data.global.resetEpoch;
  }

  auto renderCoverFallback = [&]() -> bool {
    if (coverPath.empty() || !Storage.exists(coverPath.c_str())) return false;
    data.imagePath = coverPath;
    return stats_dashboard::render(renderer, data);
  };

  if (wallpaper) {
    crosspoint::sleep::wallpaper::RenderProbe probe =
        [this, &data](const crosspoint::sleep::wallpaper::SleepPick& pick) -> bool {
      data.imagePath = pick.fullPath;
      return stats_dashboard::render(renderer, data);
    };
    const auto pick = crosspoint::sleep::wallpaper::nextSleepFile(probe);
    if (pick.hasImage()) return;
  }

  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    if (FsHelpers::hasEpubExtension(path)) {
      Epub epub(path, "/.crosspoint");
      if (epub.load(false, true) && epub.generateThumbBmp(kDashboardCoverHeight)) {
        coverPath = epub.getThumbBmpPath(kDashboardCoverHeight);
      }
    } else if (FsHelpers::hasXtcExtension(path)) {
      Xtc xtc(path, "/.crosspoint");
      if (xtc.load() && xtc.generateThumbBmp(kDashboardCoverHeight)) {
        coverPath = xtc.getThumbBmpPath(kDashboardCoverHeight);
      }
    } else {
      Txt txt(path, "/.crosspoint");
      if (txt.load() && txt.generateCoverBmp()) coverPath = txt.getCoverBmpPath();
    }
  }

  if (!renderCoverFallback()) renderDefaultSleepScreen();
}

void SleepActivity::renderUntilDeathSleepScreen() const {
  // "Random Logo": show one of the full logo table (skull crests + extra user
  // logos) at random, full-frame and centered, with no moon/text indicator — just
  // the image the user sees on unlock. A fresh random pick every lock (hardware RNG).
  constexpr int kLogoSize = 384;  // multiple of 8: drawImage packs rows at width/8 bytes

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Remember which image we picked so the wake boot logo (BootActivity) can show
  // the very same one — an unlock then reveals the current wallpaper, not a
  // fresh random pick.
  const uint8_t logoIndex = static_cast<uint8_t>(esp_random() % bootlogos::kCount);
  APP_STATE.lastUntilDeathLogo = logoIndex;
  APP_STATE.saveToFile();
  const uint8_t* logo = bootlogos::kAll[logoIndex];

  renderer.clearScreen();
  renderer.drawImage(logo, (pageWidth - kLogoSize) / 2, (pageHeight - kLogoSize) / 2, kLogoSize, kLogoSize);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderCustomSleepScreen() const {
  // V2 wallpaper rotation (WallpaperPlaylistV2 via the wallpaper facade). The
  // playlist is filtered to the chosen wallpaperFormat, ordered newest-first,
  // honours pause + the /sleep pause overflow, and falls back to /sleep.{pxc,bmp}
  // at the SD root. The facade picks the next candidate and drives this probe,
  // retrying another file if a render fails transiently.
  //
  // The probe renders by extension: .pxc through the grayscale pxc path,
  // everything else through the existing bitmap path. Returning false on an
  // open/parse failure lets the facade try the next candidate.
  crosspoint::sleep::wallpaper::RenderProbe probe =
      [this](const crosspoint::sleep::wallpaper::SleepPick& pick) -> bool {
    const std::string& p = pick.fullPath;
    if (p.size() >= 4 && strcasecmp(p.c_str() + p.size() - 4, ".pxc") == 0) {
      // FAST = one 1-bit dithered refresh (~0.5 s panel time), same path the
      // unlock banner uses; PRETTY = OEM multi-pass grayscale (~4-6 s on X3).
      const bool grayscale = SETTINGS.sleepImageQuality == CrossPointSettings::SLEEP_IMG_PRETTY;
      return renderPxcSleepScreen(renderer, p, /*extraOverlay=*/nullptr, /*drawInfoOverlay=*/true, grayscale);
    }
    HalFile f;
    if (!Storage.openFileForRead("SLP", p, f)) return false;
    Bitmap bitmap(f, true);
    if (bitmap.parseHeaders() != BmpReaderError::Ok) {
      f.close();
      return false;
    }
    renderBitmapSleepScreen(bitmap, p);
    f.close();
    return true;
  };

  const auto pick = crosspoint::sleep::wallpaper::nextSleepFile(probe);
  if (!pick.hasImage()) {
    renderDefaultSleepScreen();
  }
}

void SleepActivity::renderDefaultSleepScreen() const {
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  renderer.drawImage(Logo120, (pageWidth - 120) / 2, (pageHeight - 120) / 2, 120, 120);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, tr(STR_CROSSPOINT), true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, tr(STR_SLEEPING));

  // Make sleep screen dark unless light is selected in settings
  if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    renderer.invertScreen();
  }

  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

void SleepActivity::renderBitmapSleepScreen(const Bitmap& bitmap, const std::string& sourcePath) const {
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

  // Info overlay (filename / favorite badge), drawn after the invert filter so it
  // stays readable, and redrawn in every grayscale pass so it composites solid.
  drawSleepInfoOverlay(renderer, sourcePath);

  if (hasGreyscale) {
    // OEM grayscale pipeline base: on X3 this displays the frame with the
    // dedicated "AA-pre-BW(mid)" differential waveform, leaving every pixel
    // in the calibrated state the gray nudge refresh expects; on X4 it is a
    // plain HALF refresh (previous behavior).
    renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  if (hasGreyscale) {
    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawSleepInfoOverlay(renderer, sourcePath);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawSleepInfoOverlay(renderer, sourcePath);
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
