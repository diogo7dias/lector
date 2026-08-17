#pragma once

#include <functional>
#include <string>
#include <vector>

#include "FontInstaller.h"
#include "SdCardFont.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// JSON schema version of the fonts.json manifest. The canonical version for
// the build tooling lives in lib/EpdFont/scripts/cpfont_version.py. This
// firmware-side copy must be bumped manually when the firmware is updated to
// support a new manifest schema.
#define FONTS_MANIFEST_VERSION 1

#ifndef FONT_MANIFEST_URL
// Manifest + .cpfont assets are published by .github/workflows/release-fonts.yml
// to the crosspoint-fonts repo under the "sd-fonts-m<META>-b<BIN>" tag. The tag
// pattern must stay in sync with the workflow; it derives its version numbers
// from lib/EpdFont/scripts/cpfont_version.py.
#define FONT_MANIFEST_URL_STRINGIFY_INNER(x) #x
#define FONT_MANIFEST_URL_STRINGIFY(x) FONT_MANIFEST_URL_STRINGIFY_INNER(x)
#define FONT_MANIFEST_URL                                                                                           \
  "https://github.com/crosspoint-reader/crosspoint-fonts/releases/download/sd-fonts-m" FONT_MANIFEST_URL_STRINGIFY( \
      FONTS_MANIFEST_VERSION) "-b" FONT_MANIFEST_URL_STRINGIFY(CPFONT_VERSION) "/fonts.json"
#endif

class FontDownloadActivity : public Activity {
 public:
  explicit FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state_ == LOADING_MANIFEST || state_ == DOWNLOADING ||
           // The download is synchronous and blocks the main loop until it
           // completes, so activityManager.preventAutoSleep() is never polled
           // during downloading.
           state_ == COMPLETE || state_ == ERROR;
  }
  bool skipLoopDelay() override { return true; }

 private:
  enum State {
    WIFI_SELECTION,
    LOADING_MANIFEST,
    FAMILY_LIST,
    DOWNLOADING,
    COMPLETE,
    ERROR,
  };

  struct ManifestFile {
    std::string name;
    size_t size = 0;
    uint32_t crc32 = 0;
  };

  struct ManifestFamily {
    std::string name;
    std::string description;
    std::vector<std::string> styles;
    std::vector<ManifestFile> files;
    size_t totalSize = 0;
    bool installed = false;
    bool hasUpdate = false;
  };

  State state_ = WIFI_SELECTION;
  FontInstaller fontInstaller_;
  ButtonNavigator buttonNavigator_;

  // Manifest data
  std::string baseUrl_;
  std::vector<ManifestFamily> families_;
  int selectedIndex_ = 0;

  // Download progress
  size_t currentFileIndex_ = 0;
  size_t currentFileTotal_ = 0;
  size_t fileProgress_ = 0;
  size_t fileTotal_ = 0;
  int downloadingFamilyIndex_ = 0;
  std::string errorMessage_;
  bool cancelRequested_ = false;
  // Which attempt at the current file is running, 0 while the first one is in
  // flight. Shown on the progress screen so a slow retry does not look like a
  // hang.
  int retryAttempt_ = 0;

  /** Attempts per file and per manifest fetch, first try included. */
  static constexpr int MAX_ATTEMPTS = 3;
  /** Pause before a retry; the second retry waits twice this. */
  static constexpr uint32_t RETRY_DELAY_MS = 1500;

  void onWifiSelectionComplete(bool success);
  bool fetchAndParseManifest();
  /**
   * Downloads one family's files. Returns false with errorMessage_ set when a
   * file could not be fetched or did not survive its checks, and leaves the
   * state alone so a batch can carry on with the next family.
   */
  bool downloadFamily(ManifestFamily& family);
  /** downloadFamily() plus the COMPLETE or ERROR screen, for a single pick. */
  void downloadSingleFamily(ManifestFamily& family);
  /**
   * Fetches one font file, checks it, and retries a few times before giving up.
   *
   * A dropped connection, a stalled read, or a body that arrives corrupted are
   * all normal over patchy WiFi, and one of them used to end the whole install.
   * Returns false only once every attempt has failed, or straight away when the
   * reader cancelled.
   */
  bool downloadFileWithRetries(const ManifestFile& file, const char* destPath);
  /** Waits `ms`, staying responsive to a cancel press. Returns false if cancelled. */
  bool waitBeforeRetry(uint32_t ms);
  void downloadAll();
  void updateAll();
  /** Downloads every family `wanted` selects, carrying on past any that fail. */
  void runBatch(const std::function<bool(const ManifestFamily&)>& wanted);
  static bool computeFileCrc32(const char* path, uint32_t& outCrc);
  bool showDownloadAllRow() const;
  bool showUpdateAllRow() const;
  int specialRowCount() const;
  bool isDownloadAllRow(int index) const;
  bool isUpdateAllRow(int index) const;
  bool isSelectedFamilyDeletable() const;
  void promptDeleteSelectedFamily();
  void onDeleteConfirmationResult(const ActivityResult& result);
  int familyIndexFromList(int listIndex) const { return listIndex - specialRowCount(); }
  int listItemCount() const;
  size_t totalDownloadSize() const;
  size_t totalUpdateSize() const;
  static std::string formatSize(size_t bytes);
};
