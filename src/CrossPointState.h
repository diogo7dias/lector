#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  std::string openEpubPath;
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // Wallpaper the last sleep screen actually rendered, or empty when the sleep screen
  // was not a wallpaper. Deep sleep is a chip reset, so the wake has no other way to
  // know what the panel is holding. The wake uses it to tell a wallpaper sleep from a
  // logo one (the two take different unlock paths), and the reader menu uses it to
  // favorite, pause or delete the image the lock screen last showed.
  std::string lastSleepWallpaperPath;

  // Index into bootlogos::kAll of the crest the last logo sleep screen drew. Deep sleep
  // is a chip reset, so the wake has no other way to know which one is on the panel; the
  // unlock redraws that same crest instead of picking a fresh one, which would read as
  // the screen changing on its own.
  uint8_t lastBootLogo = 0;

  // Sleep wallpaper index (/.crosspoint/sleep_index.bin) snapshot + rotation
  // cursor. The index file itself lives on SD; these scalars are all the RAM
  // the rotation ever holds. Snapshot halves detect folder changes at cold
  // boot; cursor fields drive the shuffled no-repeat lap; sleepFreshNext marks
  // the start of the fresh (appended, served-next) record region. Defaults
  // mean "no index yet" so a missing key forces a first build.
  uint32_t sleepIndexLiveCount = 0;     // wallpapers counted at last reconcile
  uint32_t sleepIndexFingerprint = 0;   // order-independent folder fingerprint
  uint8_t sleepIndexDirId = 0;          // 0 = /sleep, 1 = /.sleep
  bool sleepIndexDirty = false;         // a mutation hook flagged the folder
  bool sleepIndexNeedsRebuild = false;  // pick exhausted its skip budget
  // Records whose file is gone (deleted or moved out) but whose slot is still
  // in the index. Maintained in place by the delete hooks so a delete costs no
  // folder walk; the pick skips these slots and a compacting rebuild is
  // deferred until the lap ends or the ratio passes deadSlotsDemandRebuild().
  uint32_t sleepIndexDeadSlots = 0;
  // Last live directory slot at the last reconcile. On the Xteink boards a
  // battery lock is a full power cut, so every unlock arrives as a power-on
  // reset — this probe result is what lets those boots skip the folder walk
  // when nothing was added (FAT appends extend the slot tail).
  uint32_t sleepIndexTailSlot = 0;
  // The sleep folder's own FAT modify date and time at the last reconcile,
  // packed as (date << 16) | time. Second signal beside the slot tail: a delete
  // frees its slots in place, so files added later can reuse those holes and
  // leave the tail identical while the folder's timestamp still moves. 0 means
  // the driver reported none, and the comparison then falls back to the tail.
  uint32_t sleepIndexDirStamp = 0;
  uint32_t sleepCursorPos = 0;
  uint32_t sleepCursorMult = 1;
  uint32_t sleepCursorOff = 0;
  uint32_t sleepCursorSeededCount = 0;
  bool sleepCursorSeeded = false;
  uint32_t sleepFreshNext = 0;

  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);
};

// Helper macro to access state
#define APP_STATE CrossPointState::getInstance()
