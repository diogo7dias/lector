#pragma once
#include <cstdint>
#include <string>

class CrossPointState {
  // Static instance
  static CrossPointState instance;

 public:
  std::string openEpubPath;
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
  // in-memory fields so the engine compiles.
  // Basename of the last wallpaper shown (rotation dedup + paused re-show target).
  std::string lastShownSleepFilename;
  // Direct-pick engine's OWN lexicographic rotation cursor, kept separate from
  // lastShownSleepFilename so the buffer engine (which writes lastShownSleepFilename)
  // can no longer reset the direct walk's progress when the heap gate flips between
  // the two engines (the ~400-file "same few repeat" bug).
  std::string lastDirectPickFilename;
  // Full path of the last wallpaper rendered (paused-rotation re-show).
  std::string lastSleepWallpaperPath;
  // Rotation paused via the in-book triage menu: re-show the current wallpaper.
  bool wallpaperRotationPaused = false;

  // --- Sleep wallpaper index rotation (SleepWallpaperIndexStore + SleepRotationPolicy) ---
  // Snapshot of the built /.crosspoint/sleep_index.bin: record count and the
  // /sleep content fingerprint it was built from (rebuild gate).
  uint32_t sleepIndexCount = 0;
  uint32_t sleepIndexFingerprint = 0;
  // Shuffled-lap rotation cursor (sleep_rotation::Cursor), persisted across the
  // deep-sleep power cut so every lap shows each wallpaper exactly once.
  uint32_t sleepCursorPos = 0;
  uint32_t sleepCursorMult = 1;
  uint32_t sleepCursorOff = 0;
  uint32_t sleepCursorSeededCount = 0;
  bool sleepCursorSeeded = false;
  // Transient: wallpapers demoted to "/sleep pause" pending a home-screen notice.
  uint16_t pendingSleepWallpapersMovedToPause = 0;

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
