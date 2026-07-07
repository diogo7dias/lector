#pragma once
#include <cstdint>
#include <string>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  static constexpr uint8_t SLEEP_RECENT_COUNT = 16;

  std::string openEpubPath;
  uint16_t recentSleepImages[SLEEP_RECENT_COUNT] = {};  // circular buffer of recent wallpaper indices
  uint8_t recentSleepPos = 0;                           // next write slot
  uint8_t recentSleepFill = 0;                          // valid entries (0..SLEEP_RECENT_COUNT)
  uint8_t readerActivityLoadCount = 0;
  bool lastSleepFromReader = false;
  bool showBootScreen = true;
  // Running tally of pages turned forward, shown on the home header. Reset from
  // the in-book menu. Incremented in memory on each forward page turn and
  // persisted opportunistically with the rest of the state (no per-page SD write).
  uint32_t sessionPagesRead = 0;
  // Index (0..4) of the skull crest the "Until Death" sleep screen picked at
  // lock time. Reused as the wake boot logo so an unlock reveals the same
  // wallpaper instead of a fresh random crest.
  uint8_t lastUntilDeathLogo = 0;

  // --- Sleep wallpaper V2 (WallpaperPlaylistV2 rotation engine) ---
  // NOTE: JSON persistence for these is added in a later commit; here they are
  // in-memory fields so the engine + FavoriteImage shim compile.
  // Basename of the last wallpaper shown (rotation dedup + direct-pick cursor).
  std::string lastShownSleepFilename;
  // Full path of the last wallpaper rendered (paused-rotation re-show).
  std::string lastSleepWallpaperPath;
  // Rotation paused via the in-book triage menu: re-show the current wallpaper.
  bool wallpaperRotationPaused = false;
  // Sticky: favorites alone saturate the /sleep cap, new uploads blocked.
  bool sleepFavoritesCapReached = false;
  // Transient: wallpapers demoted to "/sleep pause" pending a home-screen notice.
  uint16_t pendingSleepWallpapersMovedToPause = 0;
  // Hard cap on /sleep contents; mirrors crosspoint::sleep::v2::kSleepFolderCap.
  static constexpr uint16_t SLEEP_FAVORITES_MAX = 500;

  // Returns true if idx was shown within the last checkCount picks.
  // Walks backwards from the most recently written slot.
  bool isRecentSleep(uint16_t idx, uint8_t checkCount) const;

  void pushRecentSleep(uint16_t idx);
  ~CrossPointState() = default;

  // Get singleton instance
  static CrossPointState& getInstance() { return instance; }

  bool saveToFile() const;

  bool loadFromFile();

 private:
  bool loadFromBinaryFile();
};

// Helper macro to access settings
#define APP_STATE CrossPointState::getInstance()
