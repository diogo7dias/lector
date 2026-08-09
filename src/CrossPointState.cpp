#include "CrossPointState.h"

#include <Logging.h>

void CrossPointState::toJson(JsonDocument& doc) const {
  doc["openEpubPath"] = openEpubPath;
  doc["readerActivityLoadCount"] = readerActivityLoadCount;
  doc["lastSleepFromReader"] = lastSleepFromReader;
  doc["showBootScreen"] = showBootScreen;
  doc["lastSleepWallpaperPath"] = lastSleepWallpaperPath;
  doc["sleepIndexLiveCount"] = sleepIndexLiveCount;
  doc["sleepIndexFingerprint"] = sleepIndexFingerprint;
  doc["sleepIndexDirId"] = sleepIndexDirId;
  doc["sleepIndexDirty"] = sleepIndexDirty;
  doc["sleepIndexNeedsRebuild"] = sleepIndexNeedsRebuild;
  doc["sleepIndexTailSlot"] = sleepIndexTailSlot;
  doc["sleepCursorPos"] = sleepCursorPos;
  doc["sleepCursorMult"] = sleepCursorMult;
  doc["sleepCursorOff"] = sleepCursorOff;
  doc["sleepCursorSeededCount"] = sleepCursorSeededCount;
  doc["sleepCursorSeeded"] = sleepCursorSeeded;
  doc["sleepFreshNext"] = sleepFreshNext;
}

bool CrossPointState::fromJson(JsonVariantConst doc) {
  openEpubPath = doc["openEpubPath"] | "";
  readerActivityLoadCount = doc["readerActivityLoadCount"] | static_cast<uint8_t>(0);
  lastSleepFromReader = doc["lastSleepFromReader"] | false;
  showBootScreen = doc["showBootScreen"] | true;
  lastSleepWallpaperPath = doc["lastSleepWallpaperPath"] | std::string("");
  // Missing keys fall back to "no index yet" defaults, so a pre-index
  // state.json (or one from the removed recency-buffer era) forces a clean
  // first build instead of misreading stale fields.
  sleepIndexLiveCount = doc["sleepIndexLiveCount"] | static_cast<uint32_t>(0);
  sleepIndexFingerprint = doc["sleepIndexFingerprint"] | static_cast<uint32_t>(0);
  sleepIndexDirId = doc["sleepIndexDirId"] | static_cast<uint8_t>(0);
  sleepIndexDirty = doc["sleepIndexDirty"] | false;
  sleepIndexNeedsRebuild = doc["sleepIndexNeedsRebuild"] | false;
  sleepIndexTailSlot = doc["sleepIndexTailSlot"] | static_cast<uint32_t>(0);
  sleepCursorPos = doc["sleepCursorPos"] | static_cast<uint32_t>(0);
  sleepCursorMult = doc["sleepCursorMult"] | static_cast<uint32_t>(1);
  sleepCursorOff = doc["sleepCursorOff"] | static_cast<uint32_t>(0);
  sleepCursorSeededCount = doc["sleepCursorSeededCount"] | static_cast<uint32_t>(0);
  sleepCursorSeeded = doc["sleepCursorSeeded"] | false;
  sleepFreshNext = doc["sleepFreshNext"] | static_cast<uint32_t>(0);
  return true;
}
