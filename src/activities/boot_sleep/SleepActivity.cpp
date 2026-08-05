#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>

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
#include "images/Logo120.h"
#include "images/MoonIcon.h"
#include "reading_stats/ReaderStatsSession.h"
#include "reading_stats/ReadingStatsClock.h"
#include "reading_stats/ReadingStatsStore.h"
#include "reading_stats/SdStatsFiles.h"
#include "sleep/WallpaperNames.h"
#include "util/TaskWatchdog.h"

static_assert(CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD == stats_dashboard::kStatsDashboardMode);

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

// A FAT directory is a flat array of fixed-size slots, so a random wallpaper can be
// reached by SEEKING to a random slot rather than walking every entry. SdFat's
// openNext() reads from the directory's current position, which makes the jump legal:
// it returns the first live entry at or after wherever the position was left.
//
// Cost is ~2*log2(entries) probes (roughly 25 reads for a 4000-file folder) instead of
// one read per file. The old full walk took seconds on a large folder and could wedge
// sleep entry outright.
//
// Fairness: a file consumes one slot per 13 characters of long name, plus one, so a
// plain random slot favours long-named files in proportion to their name length, and a
// forward walk from the landing slot compounds it by giving each file the dead slots
// before it. The jump below removes both effects with rejection sampling: a landing is
// accepted only when it is the FIRST slot of a wallpaper's entry, so every wallpaper has
// exactly one accepting slot regardless of how many it spans. Rejections re-roll, and
// after MAX_JUMP_TRIES the caller's reservoir walk — already exactly uniform — takes
// over, so the pick is fair either way.
constexpr size_t DIR_SLOT_BYTES = 32;
// 131072 slots — far past any real wallpaper folder, and only a bound for the doubling
// probe, not an allocation.
constexpr size_t MAX_DIR_BYTES = 4u * 1024u * 1024u;
// How many landings to try before deferring to the caller's full uniform walk. Accept
// probability is (wallpapers / slots), so a folder of long-named files rejects more
// often; eight tries keeps the fallback rare without making sleep entry slow.
constexpr int MAX_JUMP_TRIES = 8;

using crosspoint::sleep::isWallpaperName;

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

// True when `offset` is the first slot of the entry a seek there returns.
//
// A long name spans several slots, and seeking into the middle of that chain still
// returns the same entry — which is exactly what makes a plain random slot favour long
// names. Probing the slot before the landing separates the two cases without decoding
// the chain: if it yields the same entry, the landing was inside the chain, not at its
// start. Deriving the chain length from the name instead would be wrong, because a name
// that fits 8.3 has no long-name slots at all.
bool landsOnEntryStart(HalFile& dir, const size_t offset, const char* name) {
  if (offset == 0) return true;  // nothing can precede the first slot
  if (!dir.seekSet(offset - DIR_SLOT_BYTES)) return false;
  auto probe = dir.openNextFile();
  if (!probe) return false;
  char previous[256];  // FAT long-file-name maximum (255 characters plus terminator)
  probe.getName(previous, sizeof(previous));
  probe.close();
  return strcmp(previous, name) != 0;
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

  char name[256];  // FAT long-file-name maximum (255 characters plus terminator)
  for (int tries = 0; tries < MAX_JUMP_TRIES; tries++) {
    const size_t start = static_cast<size_t>(random(static_cast<long>(slots))) * DIR_SLOT_BYTES;
    if (!dir.seekSet(start)) continue;
    auto entry = dir.openNextFile();
    if (!entry) continue;  // landed past the last entry
    const bool isDir = entry.isDirectory();
    entry.getName(name, sizeof(name));
    entry.close();
    // Re-roll rather than walking forward: a forward walk is what hands each file the
    // dead and non-wallpaper slots ahead of it.
    if (isDir || !isWallpaperName(name)) continue;
    if (!landsOnEntryStart(dir, start, name)) continue;
    return std::string(name);
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
  previousWallpaper = APP_STATE.lastSleepWallpaperPath;
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

  // Freeze keeps whatever is already on the panel, so it must run before both the
  // popup and the deep clean — either would destroy the very thing it preserves.
  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::FREEZE) {
    return renderFreezeSleepScreen();
  }

  // The "Entering sleep" popup is a progress note for a lock that takes a moment.
  // A wallpaper lock does not need it: deepCleanPanel is about to blank the screen
  // anyway, so the popup is one extra differential paint the user sees for an
  // instant and then loses. The old fork skipped it on this path for the same
  // reason (its directWallpaperLock).
  const bool paintsWallpaper =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM && !APP_STATE.lastSleepFromReader);
  if (!paintsWallpaper) {
    // Show popup with reader orientation only when going to sleep from reader
    if (APP_STATE.lastSleepFromReader) {
      ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
      renderer.setOrientation(GfxRenderer::Orientation::Portrait);
    } else {
      GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    }
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
    case (CrossPointSettings::SLEEP_SCREEN_MODE::STATS_DASHBOARD):
      return renderStatsDashboardSleepScreen();
    default:
      return renderDefaultSleepScreen();
  }
}

void SleepActivity::renderStatsDashboardSleepScreen() const {
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

  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    // Thumb generation is SD->SD image decode; the framebuffer is not needed until
    // the render below, so lend its 48KB to the decoder. The inflate window+state
    // then comes from the loaned block instead of the heap at the sleep-entry
    // low-water mark. The render path below starts with a full clearScreen, so the
    // scribbled buffer is fine. The loan MUST end before any render call.
    GfxRenderer::FrameBufferLoan loan(renderer);
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

  if (coverPath.empty() || !Storage.exists(coverPath.c_str())) {
    return renderDefaultSleepScreen();
  }
  data.imagePath = coverPath;
  if (!stats_dashboard::render(renderer, data)) renderDefaultSleepScreen();
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
      const SleepInfoOverlayScope overlayScope("/sleep.bmp");
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
  // Quality is the user's call: Pretty is the 3-pass grayscale above, Fast is a single
  // 1-bit pass. Three panel refreshes instead of one is real time spent going to sleep,
  // which is worth trading away on a dithered image.
  const bool pxcGrayscale = SETTINGS.sleepImageQuality == CrossPointSettings::SLEEP_QUALITY_PRETTY;
  {
    const SleepInfoOverlayScope overlayScope("/sleep.pxc");
    if (renderPxcSleepScreen(renderer, "/sleep.pxc", pxcGrayscale, HalDisplay::HALF_REFRESH, &drawSleepInfoOverlay)) {
      LOG_INF("SLP", "Loaded: /sleep.pxc");
      APP_STATE.lastSleepWallpaperPath = "/sleep.pxc";
      if (dir) dir.close();
      return;
    }
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
    std::string chosen;

    // Rotation paused: keep showing the wallpaper that is already up instead of
    // picking a new one. Only honoured while that file is still sitting in this
    // folder — favouriting renames it, pausing moves it out, and deleting it from
    // the browser removes it, so a stale name here must fall through to a normal
    // pick rather than leaving the user with a blank sleep screen they cannot
    // explain.
    if (SETTINGS.wallpaperRotationPaused && !previousWallpaper.empty()) {
      const std::string prefix = std::string(sleepDir) + "/";
      if (previousWallpaper.rfind(prefix, 0) == 0 && Storage.exists(previousWallpaper.c_str())) {
        chosen = previousWallpaper.substr(prefix.size());
        LOG_INF("SLP", "rotation paused, holding %s", chosen.c_str());
      } else {
        LOG_INF("SLP", "rotation paused but held wallpaper is gone; picking a new one");
      }
    }

    // Fast path: seek straight to a random directory slot (see pickWallpaperByJump).
    const bool heldByPause = !chosen.empty();
    const uint32_t pickStartMs = millis();
    if (chosen.empty()) chosen = pickWallpaperByJump(dir);
    if (!chosen.empty()) {
      LOG_INF("SLP", "jump pick in %ums", static_cast<unsigned>(millis() - pickStartMs));
    }

    // Never show the same wallpaper twice in a row. Each lock picks independently, so
    // the pick can legitimately land on the file already on the panel; a couple of
    // re-rolls make a visible repeat unlikely without pretending the folder holds more
    // than it does. A one-wallpaper folder simply keeps showing it, which is correct,
    // and a paused rotation is meant to repeat, so it is left alone.
    if (!heldByPause && !chosen.empty() && !previousWallpaper.empty()) {
      const std::string prefix = std::string(sleepDir) + "/";
      for (int retry = 0; retry < 2 && prefix + chosen == previousWallpaper; retry++) {
        const std::string again = pickWallpaperByJump(dir);
        if (again.empty()) break;  // jump gave up; keep what we have
        chosen = again;
      }
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
      const SleepInfoOverlayScope overlayScope(filename);
      if (hasPxcExtension(chosen)) {
        if (renderPxcSleepScreen(renderer, filename, pxcGrayscale, HalDisplay::HALF_REFRESH, &drawSleepInfoOverlay)) {
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
// One blank FULL pass is not enough on a panel that has been used. Device test
// 2026-07-25: locking to a wallpaper straight after a cold boot is perfectly
// clean, while locking after normal use ghosts faintly — so the blank itself and
// the driver are fine, and what defeats it is accumulated charge. The old fork
// never let that build up: DisplayRefreshPolicy promoted every 13th consecutive
// FAST to a cleaning refresh. The re-base dropped that policy on purpose, and it
// stays dropped, because capping it would put a flash back into reading and the
// Refresh Frequency setting offers "Never" precisely to avoid that.
//
// So the panel is cleaned here instead, where nothing is waiting: drive it to
// black, then to white, each with the multi-flash GC waveform. The black pass is
// what does the work — pushing every pixel to the opposite extreme releases the
// charge a white-only pass leaves sitting in pixels that were already white.
//
// FULL is the waveform upstream deliberately avoids for sleep (#2471's blinking
// complaint); two of them cost roughly 3 s. At this point sleep is committed:
// there is no page turn to delay and the flashing is hidden behind the lock.
//
// Not called for the quick-resume face, which must keep the frame it inherits.
void SleepActivity::renderFreezeSleepScreen() const {
  // The panel is already holding the last rendered page and nothing here clears it.
  // Draw a 2px border around the screen edge and let a differential refresh add just
  // that border, leaving the page underneath untouched.
  const int w = renderer.getScreenWidth();
  const int h = renderer.getScreenHeight();
  const bool black = SETTINGS.sleepFrameColor == CrossPointSettings::SLEEP_FRAME_BLACK;
  renderer.drawRect(0, 0, w, h, 2, black);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

// One blank FULL pass before painting a sleep face, exactly as the pre-rebase fork did
// it (its RefreshIntent::DeepClean was Buffer + FULL over a white screen). Lock can
// happen over any screen, and every sleep render below uses the calibrated differential
// HALF/graybase waveforms, which would otherwise leave the prior content ghosting
// through the wallpaper for the whole sleep.
//
// This deliberately does NOT try to be a stronger wipe. Escalating it here (black-then-
// white, then three cycles of that) was tried and did not stop the ghost, because the
// ghost was never a wipe problem: the pre-rebase fork ghosted nothing with this single
// pass. What it also had, and what the rebase had dropped, is the anti-ghosting cap in
// HalDisplay that stops a long run of FAST passes from trapping charge in the first
// place. That cap is back; this stays as it was.
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

  // Drawn after the filter so the label is never inverted along with the image,
  // and once per pass below for the same reason the bitmap is. No-ops unless a
  // SleepInfoOverlayScope named a wallpaper, so the cover face draws nothing.
  drawSleepInfoOverlay(renderer);

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
    drawSleepInfoOverlay(renderer);
    renderer.copyGrayscaleLsbBuffers();

    bitmap.rewindToData();
    renderer.clearScreen(0x00);
    renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    renderer.drawBitmap(bitmap, x, y, pageWidth, pageHeight, cropX, cropY);
    drawSleepInfoOverlay(renderer);
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
  if (gpio.deviceIsX3()) {
    // The controller still holds the displayed page, so its differential base
    // waveform can add the moon without a full-screen flash.
    renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
  } else {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }
}

void SleepActivity::renderBlankSleepScreen() const {
  renderer.clearScreen();
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
