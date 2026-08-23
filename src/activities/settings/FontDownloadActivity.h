#pragma once

#include <FontManifestParser.h>

#include <cstdint>
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

// Repository that hosts the font releases. A build can point the reader at a
// different library by defining FONT_MANIFEST_REPO; the tag composition below
// stays intact, so the device still asks for the release matching its own
// manifest and .cpfont versions.
#ifndef FONT_MANIFEST_REPO
#define FONT_MANIFEST_REPO "crosspoint-reader/crosspoint-fonts"
#endif

#ifndef FONT_MANIFEST_URL
// Manifest + .cpfont assets are published by .github/workflows/release-fonts.yml
// to the fonts repo under the "sd-fonts-m<META>-b<BIN>" tag. The tag
// pattern must stay in sync with the workflow; it derives its version numbers
// from lib/EpdFont/scripts/cpfont_version.py.
#define FONT_MANIFEST_URL_STRINGIFY_INNER(x) #x
#define FONT_MANIFEST_URL_STRINGIFY(x) FONT_MANIFEST_URL_STRINGIFY_INNER(x)
#define FONT_MANIFEST_URL                                                                               \
  "https://github.com/" FONT_MANIFEST_REPO "/releases/download/sd-fonts-m" FONT_MANIFEST_URL_STRINGIFY( \
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

  // The manifest model lives in the parser: fixed buffers, nothrow growth, no DOM.
  using ManifestFile = FontManifestFile;
  using ManifestFamily = FontManifestFamily;

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

  // What the panel is currently showing. A differential refresh does not fully drive
  // black pixels back to white, so switching screens (list -> progress -> result) or
  // rewriting the "(n/total)" and retry lines leaves the old text behind as grey
  // residue. Those moments take a cleanup pass; the progress bar's own ticks do not.
  State lastDisplayedState_ = WIFI_SELECTION;
  int lastDisplayedFamilyIndex_ = -1;
  size_t lastDisplayedFileIndex_ = SIZE_MAX;
  int lastDisplayedRetry_ = -1;
  // Which PROGRESS_STEP_PERCENT bucket the drawn bar sits in, -1 before the first paint.
  int lastDrawnProgressStep_ = -1;

  /**
   * Attempts per file and per manifest fetch, first try included. A partial file
   * is resumed rather than refetched, so a later attempt costs only the bytes
   * still missing and being generous here is cheap.
   */
  /** Read size for streaming the manifest off the SD card into the parser. */
  static constexpr size_t MANIFEST_CHUNK = 1024;
  static constexpr int MAX_ATTEMPTS = 5;
  /** Pause before a retry; each further retry waits a multiple of this. */
  static constexpr uint32_t RETRY_DELAY_MS = 1500;
  /** Longest wait for the access point to come back before an attempt. */
  static constexpr uint32_t WIFI_WAIT_MS = 20000;
  /** How often a reconnect is asked for again while waiting for the link. */
  static constexpr uint32_t RECONNECT_EVERY_MS = 5000;
  /**
   * How far the progress bar must move before the panel is redrawn. The download
   * callback fires once per network chunk, which is hundreds of times per file, and
   * every one of those was a full-screen refresh for a bar that had not visibly
   * moved. Redrawing in steps keeps the bar honest and the panel clean.
   */
  static constexpr int PROGRESS_STEP_PERCENT = 5;

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
  /**
   * Waits for the station to be associated again, asking for a reconnect first.
   * A download run is minutes long over a link the reader is often at the edge
   * of, and every attempt after a drop fails instantly unless it waits here.
   * Returns false when the link did not come back or the reader cancelled.
   */
  bool waitForWifi();
  /**
   * True when `destPath` already holds this exact file: right size, right CRC32.
   * A rerun after a failure then costs nothing for the files already on the card.
   */
  static bool fileAlreadyInstalled(const ManifestFile& file, const char* destPath);
  /**
   * Moves a verified staging file onto the name the renderer reads, replacing
   * the previous copy. Called only once the download passed every check, so the
   * font on the card is never the one being replaced mid-flight.
   */
  static bool promoteStagedFile(const char* partPath, const char* destPath);
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
