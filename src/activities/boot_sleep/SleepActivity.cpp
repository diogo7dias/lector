#include "SleepActivity.h"

#include <Epub.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Txt.h>
#include <Xtc.h>
#include <esp_random.h>

#include <cmath>
#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "PxcSleepRenderer.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "SleepTiming.h"
#include "activities/reader/ReaderUtils.h"
#include "components/UITheme.h"
#include "dev/LockLab.h"
#include "fontIds.h"
#include "sleep/DirSlotProbe.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperNames.h"
#include "util/DeferredFavorite.h"
#include "util/FavoriteImageNames.h"
#include "util/TaskWatchdog.h"

namespace {

// The value passed for renderPxcSleepScreen's oneBitRefresh on the sleep faces, where
// grayscale is always true and the argument is therefore never read. Named rather than
// spelled as a literal so nobody reads it as a waveform this path chooses; it is the
// parameter's own default. The 1-bit callers that DO read it (BootActivity,
// PxcViewerActivity) pass their own.
constexpr HalDisplay::RefreshMode kPxcOneBitRefreshUnused = HalDisplay::HALF_REFRESH;

// A FAT directory is a flat array of fixed-size slots, so a random wallpaper can be
// reached by SEEKING to a random slot rather than walking every entry (the slot
// probing itself — entryExistsAt, liveSlotCount — lives in sleep/DirSlotProbe.h,
// shared with the wallpaper index reconcile).
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
//
// How many landings to try before deferring to the caller's full uniform walk. Accept
// probability is (wallpapers / slots), so a folder of long-named files rejects more
// often; eight tries keeps the fallback rare without making sleep entry slow.
constexpr int MAX_JUMP_TRIES = 8;

using crosspoint::sleep::DIR_SLOT_BYTES;
using crosspoint::sleep::entryExistsAt;
using crosspoint::sleep::isWallpaperName;
using crosspoint::sleep::liveSlotCount;

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
  const uint32_t slots = static_cast<uint32_t>(liveSlotCount(dir));
  if (slots == 0) return {};

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
  DeferredFavorite::waitForIdle(15000);
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
  if (APP_STATE.lastSleepFromReader) {
    ReaderUtils::applyOrientation(renderer, SETTINGS.orientation);
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
    renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  } else {
    GUI.drawPopup(renderer, tr(STR_ENTERING_SLEEP));
  }
  SleepTiming::mark("popup");

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

void SleepActivity::renderCustomSleepScreen() const {
#ifdef LECTOR_LOCK_LAB
  // A Full lock run has to draw what the bench draws, or the number it reports belongs to
  // a different recipe than the picture on the panel.
  const PxcRenderOptions labOptions = locklab::optionsFor(APP_STATE.lockLab);
  const PxcRenderOptions* const pxcOptions = &labOptions;
  // Before anything is drawn, and before any sleep face is chosen: a scrub is about the
  // panel's charge history, not about which picture is going on top of it.
  const uint32_t preClearMs = locklab::applyPreClear(renderer);
  if (preClearMs != 0) LOG_INF("LAB", "pre-clear %ums", static_cast<unsigned>(preClearMs));
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
  // Always the 3-pass grayscale, never the 1-bit pass. This used to be a setting, and it
  // was the wrong thing to offer: the sleep screen is the picture the device wears while
  // it is off, so tone is the whole point of it, and the second or so the extra two panel
  // refreshes cost is spent after the user has already put the device down.
  //
  // Because pxcGrayscale is a compile-time true, the oneBitRefresh argument below is NEVER
  // READ on this path: PxcSleepRenderer.cpp only consumes it when grayscale == false. It
  // is not deletable from the signature — BootActivity (the unlock-over-wallpaper redraw)
  // and PxcViewerActivity both call this with grayscale=false and DO read it. Passing the
  // parameter's own default here rather than a literal says that plainly.
  //
  // What the panel actually does on this path is sleep_face::planFor(Face::Wallpaper,
  // /*sourceHasGrayscale=*/true, ...): a base at that plan's waveform, then the two
  // grayscale planes. Two submissions on top of the popup.
  constexpr bool pxcGrayscale = true;
  {
    const SleepInfoOverlayScope overlayScope("/sleep.pxc");
    if (renderPxcSleepScreen(renderer, "/sleep.pxc", pxcGrayscale, kPxcOneBitRefreshUnused, &drawSleepInfoOverlay,
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

    const bool heldByPause = !chosen.empty();

    // The persistent line: fresh (newly indexed) wallpapers first, then the
    // shuffled lap — every wallpaper exactly once per lap, reshuffled at the
    // wrap. Cost per sleep is one index open plus a 160-byte read per inspected
    // slot; no folder scan. Falls through to the jump pick when the index is
    // absent, built for the other folder, or declared stale.
    bool pickedFromIndex = false;
    // Rotation line position of the picked wallpaper, for the optional
    // bottom-right badge. 0/0 = unknown (jump pick, pause hold), badge hidden.
    uint32_t linePosition = 0;
    uint32_t lineTotal = 0;
    if (chosen.empty() && !APP_STATE.sleepIndexNeedsRebuild) {
      namespace windex = crosspoint::sleep::windex;
      const uint32_t indexStartMs = millis();
      windex::Reader reader;
      if (reader.open() && reader.recordCount() > 0 && strcmp(windex::dirPathForId(reader.dirId()), sleepDir) == 0) {
        SleepTiming::mark("idxopen");
        auto queueState = windex::loadQueueState();
        SleepTiming::mark("idxstate");
        const std::string prefix = std::string(sleepDir) + "/";
        const auto nameAt = [&](const size_t i) { return reader.nameAt(i); };
        // No probe: one Storage.exists() by name measured 1271 ms on a real wallpaper
        // folder, because a FAT lookup walks the directory. The open in renderChosen()
        // below answers the same question for free, and a record that fails it flags the
        // index for rebuild there. The cost of being wrong is one fallback pick, once.
        const auto liveInFolder = [](const std::string&) { return true; };
        const auto counterpart = [](const std::string& n) { return FavoriteImage::favoriteCounterpart(n); };
        auto result = sleep_queue::pickNext(queueState, reader.recordCount(), esp_random(), esp_random(), nameAt,
                                            liveInFolder, counterpart);
        // Within a lap repeats are impossible by construction; only a reseed
        // boundary can land on the wallpaper already holding the panel.
        if (!heldByPause && !result.basename.empty() && reader.recordCount() > 1 &&
            prefix + result.basename == previousWallpaper) {
          auto again = sleep_queue::pickNext(queueState, reader.recordCount(), esp_random(), esp_random(), nameAt,
                                             liveInFolder, counterpart);
          const bool wrapped = result.lapWrapped || again.lapWrapped;
          if (!again.basename.empty()) result = std::move(again);
          result.lapWrapped = wrapped;  // a wrap in either pick still ended the lap
        }
        // Lap over: every wallpaper has now been shown once, so this is the
        // cheapest possible moment to compact the holes that in-place deletes
        // left behind. Flags the next cold boot; nothing happens tonight.
        SleepTiming::mark("idxpick");
        if (result.lapWrapped) windex::noteLapWrapped();
        // Persist the advanced state even when the render below fails: the
        // cursor must step PAST a present-but-unrenderable file, or every
        // sleep would retry it and show the logo face forever. A crash before
        // the render costs one skipped wallpaper, nothing more.
        windex::storeQueueState(queueState);
        SleepTiming::mark("idxstore");
        if (result.needsRebuild) {
          // Too many dead slots this pick: use the jump pick tonight and let
          // the next cold boot rebuild the index.
          APP_STATE.sleepIndexNeedsRebuild = true;
          LOG_INF("SLP", "index stale, falling back to jump pick");
        } else if (!result.basename.empty()) {
          chosen = std::move(result.basename);
          pickedFromIndex = true;
          // Served-this-loop count, from the post-advance state: cursor.position
          // lap picks plus the drained part of the fresh region (fresh records
          // start at seededCount). 0 only right after a wrap, which means the
          // pick that COMPLETED the loop — show it as total/total, not 0.
          lineTotal = static_cast<uint32_t>(reader.recordCount());
          const uint32_t served = queueState.cursor.position + (queueState.freshNext - queueState.cursor.seededCount);
          linePosition = served == 0 || served > lineTotal ? lineTotal : served;
          LOG_INF("SLP", "index pick in %ums", static_cast<unsigned>(millis() - indexStartMs));
        }
      }
    }

    // Fast path: seek straight to a random directory slot (see pickWallpaperByJump).
    const uint32_t pickStartMs = millis();
    if (chosen.empty()) chosen = pickWallpaperByJump(dir);
    if (!chosen.empty() && !heldByPause && !pickedFromIndex) {
      LOG_INF("SLP", "jump pick in %ums", static_cast<unsigned>(millis() - pickStartMs));
    }

    // Never show the same wallpaper twice in a row. Each lock picks independently, so
    // the pick can legitimately land on the file already on the panel; a couple of
    // re-rolls make a visible repeat unlikely without pretending the folder holds more
    // than it does. A one-wallpaper folder simply keeps showing it, which is correct,
    // and a paused rotation is meant to repeat, so it is left alone. An index pick
    // handled its own reseed-boundary repeat above and must not be re-rolled here —
    // a jump re-roll would break the line's no-repeat guarantee.
    if (!heldByPause && !pickedFromIndex && !chosen.empty() && !previousWallpaper.empty()) {
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
    // Rendering a picked wallpaper. Returns false when the file cannot be opened or
    // parsed, which is also how a name that the index still lists but the folder no
    // longer holds is discovered: the pick no longer probes for liveness (see the index
    // block above), because a probe costs a full directory lookup -- 1271 ms measured on
    // a real wallpaper folder -- and the open that follows already answers the same
    // question.
    const auto renderChosen = [&](const std::string& name) {
      const auto filename = std::string(sleepDir) + "/" + name;
      LOG_INF("SLP", "Randomly loading: %s", filename.c_str());
      const SleepInfoOverlayScope overlayScope(filename, linePosition, lineTotal);
      if (hasPxcExtension(name)) {
        if (renderPxcSleepScreen(renderer, filename, pxcGrayscale, kPxcOneBitRefreshUnused, &drawSleepInfoOverlay,
                                 pxcOptions)) {
          APP_STATE.lastSleepWallpaperPath = filename;
          return true;
        }
        return false;
      }
      HalFile randFile;
      // Storage.open rather than openFileForRead: the latter calls exists() before
      // open(), which is a second full directory lookup for the same name and measured
      // as half of a 2543 ms wallpaper open. A failed open is reported here instead.
      randFile = Storage.open(filename.c_str(), O_RDONLY);
      if (!randFile) {
        LOG_INF("SLP", "wallpaper open failed: %s", filename.c_str());
        return false;
      }
      // Same rule the /sleep.bmp path above uses: stretch the tone range only when
      // the user has asked for no cover filter, so a filtered image still looks the
      // way they set it. Applies to .bmp wallpapers only; .pxc took the branch above
      // and is already quantised to four levels when the file is written.
      const bool adaptiveTone =
          SETTINGS.sleepScreenCoverFilter == CrossPointSettings::SLEEP_SCREEN_COVER_FILTER::NO_FILTER;
      Bitmap bitmap(randFile, true, adaptiveTone ? BitmapToneMapping::Adaptive : BitmapToneMapping::None);
      if (bitmap.parseHeaders() != BmpReaderError::Ok) {
        randFile.close();
        return false;
      }
      renderBitmapSleepScreen(bitmap, sleep_face::Face::Wallpaper);
      APP_STATE.lastSleepWallpaperPath = filename;
      randFile.close();
      return true;
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
        const std::string again = pickWallpaperByJump(dir);
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
    // NOTE, unresolved: on a UC8279 non-X3 board plan.base is FULL, which is what has
    // shipped and what this comment says must not happen. See the contradiction recorded
    // in test/sleep_face_paint/SleepGrayscaleBaseTest.cpp — settling it needs an X4
    // ghosting check on hardware, not a code reading.
    renderer.displayGrayscaleBase(plan.base);
  } else {
    renderer.displayBuffer(plan.base);
  }

  if (plan.grayscalePlanes) {
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
