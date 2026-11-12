#include "WallpaperPick.h"

#include <Arduino.h>
#include <Logging.h>
#include <esp_random.h>

#include <cstring>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepTiming.h"
#include "sleep/DirSlotProbe.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WallpaperNames.h"
#include "util/TaskWatchdog.h"

namespace crosspoint {
namespace sleep {
namespace {

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

}  // namespace

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

WallpaperPick pickWallpaper(HalFile& dir, const char* const sleepDir, const std::string& previousWallpaper) {
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
      // (renderCustomSleepScreen) answers the same question for free, and a record that
      // fails it flags the index for rebuild there. The cost of being wrong is one
      // fallback pick, once.
      const auto liveInFolder = [](const std::string&) { return true; };
      const auto counterpart = [](const std::string& n) { return FavoriteImage::favoriteCounterpart(n); };
      auto result = sleep_queue::pickNext(queueState, reader.recordCount(), esp_random(), esp_random(), nameAt,
                                          liveInFolder, counterpart);
      // Within a lap repeats are impossible by construction; only a reseed
      // boundary can land on the wallpaper already holding the panel.
      if (!result.basename.empty() && reader.recordCount() > 1 && prefix + result.basename == previousWallpaper) {
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
      // Persist the advanced state even when the render that follows fails: the
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
  return {std::move(chosen), pickedFromIndex, linePosition, lineTotal};
}

}  // namespace sleep
}  // namespace crosspoint
