#include "CrossPointState.h"

#include <Logging.h>

void CrossPointState::toJson(JsonDocument& doc) const {
  doc["openEpubPath"] = openEpubPath;
  doc["readerActivityLoadCount"] = readerActivityLoadCount;
  doc["lastSleepFromReader"] = lastSleepFromReader;
  doc["showBootScreen"] = showBootScreen;
  doc["quickResumeWake"] = quickResumeWake;
  doc["fastRefreshesSinceFull"] = fastRefreshesSinceFull;
  doc["readOrderCounter"] = readOrderCounter;
  doc["readingBadgesSeeded"] = readingBadgesSeeded;
  doc["lowBatteryWarned"] = lowBatteryWarned;
  doc["quickResumeTargetIsReader"] = quickResumeTargetIsReader;
  doc["pendingWakeBookPath"] = pendingWakeBookPath;
  doc["lastBootLogo"] = lastBootLogo;
  doc["lastSleepWallpaperPath"] = lastSleepWallpaperPath;
  doc["sleepIndexLiveCount"] = sleepIndexLiveCount;
  doc["sleepIndexFingerprint"] = sleepIndexFingerprint;
  doc["sleepIndexDirId"] = sleepIndexDirId;
  doc["sleepIndexDirty"] = sleepIndexDirty;
  doc["sleepIndexNeedsRebuild"] = sleepIndexNeedsRebuild;
  doc["sleepIndexTailSlot"] = sleepIndexTailSlot;
  doc["sleepIndexDirStamp"] = sleepIndexDirStamp;
  doc["sleepIndexDeadSlots"] = sleepIndexDeadSlots;
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
  quickResumeWake = doc["quickResumeWake"] | false;
  fastRefreshesSinceFull = doc["fastRefreshesSinceFull"] | static_cast<uint8_t>(0);
  readOrderCounter = doc["readOrderCounter"] | static_cast<uint32_t>(0);
  readingBadgesSeeded = doc["readingBadgesSeeded"] | false;
  lowBatteryWarned = doc["lowBatteryWarned"] | false;
  quickResumeTargetIsReader = doc["quickResumeTargetIsReader"] | false;
  pendingWakeBookPath = doc["pendingWakeBookPath"] | std::string("");
  lastBootLogo = doc["lastBootLogo"] | (uint8_t)0;
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
  sleepIndexDirStamp = doc["sleepIndexDirStamp"] | static_cast<uint32_t>(0);
  sleepIndexDeadSlots = doc["sleepIndexDeadSlots"] | static_cast<uint32_t>(0);
  sleepCursorPos = doc["sleepCursorPos"] | static_cast<uint32_t>(0);
  sleepCursorMult = doc["sleepCursorMult"] | static_cast<uint32_t>(1);
  sleepCursorOff = doc["sleepCursorOff"] | static_cast<uint32_t>(0);
  sleepCursorSeededCount = doc["sleepCursorSeededCount"] | static_cast<uint32_t>(0);
  sleepCursorSeeded = doc["sleepCursorSeeded"] | false;
  sleepFreshNext = doc["sleepFreshNext"] | static_cast<uint32_t>(0);
  return true;
}
