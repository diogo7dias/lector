#pragma once
#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <cstdint>
#include <string>

class CrossPointState : public PersistableStore<CrossPointState> {
  CrossPointState() = default;

  friend class PersistableStore<CrossPointState>;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // Wallpaper the last sleep screen actually rendered, or empty when the sleep screen
  // was not a wallpaper. Deep sleep is a chip reset, so the wake has no other way to
  // know what the panel is holding; setup() re-renders this file and composites the
  // unlock banners over it instead of showing the boot logo.
  std::string lastSleepWallpaperPath;
  // True when the sleep face was painted 1-bit, so the saved frame buffer is a faithful
  // copy of what the panel is physically holding. Only then may the wake restore that
  // frame instead of decoding the wallpaper again: a 3-pass grayscale face leaves the
  // buffer holding a plane, not the finished picture, and restoring it would paint the
  // wrong image. Measured on an X3 (lector.exp.9): re-decoding costs ~3.6s of a ~4.7s
  // wake, and it is the single largest cost in the whole wake.
  bool sleepFrameIsFaithful = false;
  static const char* getFilePath() { return "/.crosspoint/state.json"; }
  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
};

// Helper macro to access state
#define APP_STATE CrossPointState::getInstance()
