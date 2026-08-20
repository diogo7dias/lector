#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <PerfLog.h>
#include <PerfStats.h>
#include <SPI.h>
#include <WiFi.h>
#include <builtinFonts/all.h>
#include <esp_random.h>
#include <esp_system.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "PerfLogSink.h"
#include "ReaderPresetStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "UiFont.h"
#include "WakeTiming.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/util/LowBatteryNoticeActivity.h"
#include "components/UITheme.h"
#include "components/UnlockBanners.h"
#include "fontIds.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WakeFacePolicy.h"
#include "sleep/WakeRoutePolicy.h"
#include "util/BookProgressFile.h"
#include "util/ButtonNavigator.h"
#include "util/DoubleClickDetector.h"
#include "util/LowBatteryPolicy.h"
#include "util/ScreenshotUtil.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;
// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
// Vollkorn is lector's single built-in reading family (serif). Noto Serif / Noto Sans
// were dropped as reading fonts; users add more via SD-card fonts. Noto Sans survives
// only as the 8pt small font (below) and Ubuntu as the UI font.
EpdFont vollkorn14RegularFont(&vollkorn_14_regular);
EpdFont vollkorn14BoldFont(&vollkorn_14_bold);
EpdFont vollkorn14ItalicFont(&vollkorn_14_italic);
EpdFont vollkorn14BoldItalicFont(&vollkorn_14_bolditalic);
EpdFontFamily vollkorn14FontFamily(&vollkorn14RegularFont, &vollkorn14BoldFont, &vollkorn14ItalicFont,
                                   &vollkorn14BoldItalicFont);
#ifndef OMIT_FONTS
EpdFont vollkorn12RegularFont(&vollkorn_12_regular);
EpdFont vollkorn12BoldFont(&vollkorn_12_bold);
EpdFont vollkorn12ItalicFont(&vollkorn_12_italic);
EpdFont vollkorn12BoldItalicFont(&vollkorn_12_bolditalic);
EpdFontFamily vollkorn12FontFamily(&vollkorn12RegularFont, &vollkorn12BoldFont, &vollkorn12ItalicFont,
                                   &vollkorn12BoldItalicFont);
EpdFont vollkorn16RegularFont(&vollkorn_16_regular);
EpdFont vollkorn16BoldFont(&vollkorn_16_bold);
EpdFont vollkorn16ItalicFont(&vollkorn_16_italic);
EpdFont vollkorn16BoldItalicFont(&vollkorn_16_bolditalic);
EpdFontFamily vollkorn16FontFamily(&vollkorn16RegularFont, &vollkorn16BoldFont, &vollkorn16ItalicFont,
                                   &vollkorn16BoldItalicFont);
EpdFont vollkorn18RegularFont(&vollkorn_18_regular);
EpdFont vollkorn18BoldFont(&vollkorn_18_bold);
EpdFont vollkorn18ItalicFont(&vollkorn_18_italic);
EpdFont vollkorn18BoldItalicFont(&vollkorn_18_bolditalic);
EpdFontFamily vollkorn18FontFamily(&vollkorn18RegularFont, &vollkorn18BoldFont, &vollkorn18ItalicFont,
                                   &vollkorn18BoldItalicFont);
#endif  // OMIT_FONTS

EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

// Paragraph numbers only. Spleen 6x12 is a bitmap face baked at --dpi 72, so "size 12"
// means 12 pixels and every glyph lands exactly on its native grid: one-pixel stems, no
// anti-alias halo, no smear. Cozette (SMALL_FONT_ID) is also a bitmap face but is baked
// at the historic 150 dpi, i.e. 1.6x off its own 13px grid, which fattens the digits
// until 8, 9 and 0 close up at margin size. Digits here are 8px tall against Cozette's
// 13px: smaller AND cleaner. Kept off SMALL_FONT_ID so the status bar is untouched.
EpdFont paragraphNumFont(&spleen_6x12_regular);
EpdFontFamily paragraphNumFontFamily(&paragraphNumFont, &paragraphNumFont);

// The Double size: the very same Spleen cell baked at exactly 2x (24px at dpi 72), so
// each pixel becomes a 2x2 block and the shapes are identical, just larger. Verified
// glyph-by-glyph against the 1x header. A size between the two is not offered because
// a bitmap face has nothing to draw there: 1.5 pixels rounds unevenly and the stems
// come out mismatched, which is the very fault this font was brought in to cure.
EpdFont paragraphNum2xFont(&spleen_6x12_2x_regular);
EpdFontFamily paragraphNum2xFontFamily(&paragraphNum2xFont, &paragraphNum2xFont);

// The UI families ship REGULAR ONLY, and the regular face fills the family's bold slot
// so a stray BOLD request resolves to regular instead of nullptr. This is the old-Lector
// arrangement: menu weight hierarchy comes from SIZE, not from a second cut. Emphasis in
// the UI, if ever wanted, is the Paperback Look smear (GfxRenderer::setPaperbackLook),
// which thickens whatever face is loaded and costs no flash.

// Ubuntu UI family — the FULL-coverage fallback (Latin + Arabic + Hebrew + Vietnamese,
// baked with the extra script intervals). Bound permanently to UBUNTU_10/12_FONT_ID and
// used for Arabic/Hebrew UI and for the language-picker native-name list.
EpdFont ubuntu10RegularFont(&ubuntu_10_regular);
EpdFontFamily ubuntu10FontFamily(&ubuntu10RegularFont, &ubuntu10RegularFont);

EpdFont ubuntu12RegularFont(&ubuntu_12_regular);
EpdFontFamily ubuntu12FontFamily(&ubuntu12RegularFont, &ubuntu12RegularFont);

EpdFont ubuntu14RegularFont(&ubuntu_14_regular);
EpdFontFamily ubuntu14FontFamily(&ubuntu14RegularFont, &ubuntu14RegularFont);

// Cozette UI family — lector's default menu font (Latin + Cyrillic + Greek + Vietnamese;
// no Arabic/Hebrew). Sizes match the previous mature Lector: 10 = SMALL_FONT_ID,
// 12 = UI_10_FONT_ID (list rows), 14 = UI_12_FONT_ID (header title). Bound for every
// language except Arabic/Hebrew (which use the Ubuntu family at the same sizes).
EpdFont cozette10RegularFont(&cozette_10_regular);
EpdFontFamily cozette10FontFamily(&cozette10RegularFont, &cozette10RegularFont);

EpdFont cozette12RegularFont(&cozette_12_regular);
EpdFontFamily cozette12FontFamily(&cozette12RegularFont, &cozette12RegularFont);

EpdFont cozette14RegularFont(&cozette_14_regular);
EpdFontFamily cozette14FontFamily(&cozette14RegularFont, &cozette14RegularFont);

// Cozette cannot draw Arabic or Hebrew, so those two UI languages use the Ubuntu
// family. Every other language (incl. Cyrillic + Vietnamese, verified in Cozette's
// cmap) uses Cozette. Called at boot and on every in-app language change (declared
// in UiFont.h so LanguageSelectActivity can rebind after a change).
static bool uiLanguageNeedsUbuntu() {
  const Language lang = I18n::getInstance().getLanguage();
  return lang == Language::AR || lang == Language::HE;
}

void bindUiFontsForLanguage(GfxRenderer& renderer) {
  const bool useUbuntu = uiLanguageNeedsUbuntu();
  // insertFont() ignores an already-registered id, so drop the old binding first.
  // Sizes mirror the previous mature Lector (2px larger than the CrossPoint base):
  // SMALL = 10, UI_10 (list rows) = 12, UI_12 (header title) = 14. Arabic/Hebrew use the
  // Ubuntu family at the same sizes so their small text renders too (Cozette lacks AR/HE).
  renderer.removeFont(SMALL_FONT_ID);
  renderer.removeFont(UI_10_FONT_ID);
  renderer.removeFont(UI_12_FONT_ID);
  renderer.insertFont(SMALL_FONT_ID, useUbuntu ? ubuntu10FontFamily : cozette10FontFamily);
  renderer.insertFont(UI_10_FONT_ID, useUbuntu ? ubuntu12FontFamily : cozette12FontFamily);
  renderer.insertFont(UI_12_FONT_ID, useUbuntu ? ubuntu14FontFamily : cozette14FontFamily);
}

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

// The anti-ghost budget lives in RAM, and every reboot is a reset, so it has to be
// written down before the device goes away. Deep sleep does this in enterDeepSleep();
// the heap-defrag reboots below are the other routine way a session ends, and without
// this a reader who trips one (returning from KOReader sync, leaving a WiFi screen)
// hands the next session a budget of zero and delays the panel's next discharge.
static void persistAntiGhostBudget() {
  APP_STATE.fastRefreshesSinceFull = display.fastRefreshesSinceFull();
  APP_STATE.inkDebt = display.inkDebt();
  APP_STATE.saveToFile();
}

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_HOME;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  persistAntiGhostBudget();
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  persistAntiGhostBudget();
  delay(50);
  ESP.restart();
}

// Defined below setup()'s helpers; the sleep path needs it to choose the book the
// Light sleep screen names, which happens before the wake ever runs.
static std::string pickRandomRecentBookPath();
static std::string pickBootBookPath();

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

// How long the wake/unlock banners are guaranteed to stay readable. A floor on the
// banner paint, not a sleep: the next activity's own work usually outlasts it, and only
// a wake that would have covered the banners sooner ever waits here.
constexpr uint32_t UNLOCK_BANNER_MIN_VISIBLE_MS = 800;
static uint32_t unlockBannersShownAt = 0;

// Holds the wake until the banners have had their floor. Safe to call when they were
// never drawn: unlockBannersShownAt stays 0 and this returns immediately.
static void waitForUnlockBannerFloor() {
  if (unlockBannersShownAt == 0) return;
  const uint32_t elapsed = millis() - unlockBannersShownAt;
  if (elapsed < UNLOCK_BANNER_MIN_VISIBLE_MS) delay(UNLOCK_BANNER_MIN_VISIBLE_MS - elapsed);
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Only a wake that restores the exact frame already on the glass may suppress the
  // boot presentation — see WakeFacePolicy.h. A custom wallpaper is arbitrary artwork
  // that the wake paints over, so it keeps the boot presentation: the wallpaper wake
  // face (blank the panel on the button press, then draw the unlock banners in one
  // FULL pass) lives on the BootResume::Splash path, and routing a custom sleep face
  // to SplashlessWake silently deletes that clearing pass.
  const wake_face::SleepFace sleepFace = isQuickResumeSleep ? wake_face::SleepFace::QuickResumeFrame
                                         : SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM
                                             ? wake_face::SleepFace::CustomWallpaper
                                             : wake_face::SleepFace::Other;
  APP_STATE.showBootScreen = !wake_face::retainsPanelForWake(sleepFace);

  // Quick Resume promises that unlocking changes nothing: the moon appears, the moon
  // goes away, the page stays. Deep sleep is a chip reset, though, so only the reader
  // page can be rebuilt on the wake — a settings screen or a file browser cannot. When
  // the lock happens on one of those, home is painted HERE, before the moon is stamped
  // over it and the frame is saved, so the picture that sleeps is the picture that wakes.
  APP_STATE.quickResumeWake = isQuickResumeSleep;
  APP_STATE.quickResumeTargetIsReader = false;
  if (isQuickResumeSleep) {
    const bool targetIsReader = activityManager.isReaderActivity() && !APP_STATE.openEpubPath.empty();
    APP_STATE.quickResumeTargetIsReader = targetIsReader;
    if (wake_route::quickResumeNeedsHomeRepaint(targetIsReader, activityManager.isHomeActivity())) {
      activityManager.goHome();
      activityManager.loop();  // paints immediately, like goToSleep() does
    }
  }

  // The Light sleep face names the book the wake will open, so the choice is made here
  // rather than on the wake: "Open Book on Boot" picking at wake time would
  // name one book on the sleep screen and open another. Quick Resume names nothing and
  // opens nothing new, so it leaves the field empty.
  APP_STATE.pendingWakeBookPath.clear();
  if (!isQuickResumeSleep && SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT) {
    // Off names the book that is already open; the other two name the book their own
    // pick returns. A mode that finds nothing to open deliberately names nothing: falling
    // back to the last book here would force the wake into a book the ordinary routing
    // would have skipped.
    APP_STATE.pendingWakeBookPath =
        SETTINGS.bootBookMode == CrossPointSettings::BOOT_BOOK_OFF ? APP_STATE.openEpubPath : pickBootBookPath();
  }

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  // Quick resume keeps its frame, because its wake restores that exact frame. A wallpaper
  // sleep face does not: its wake blanks the panel and goes to the book, so saving 48KB to
  // the card here would only slow the lock for a frame nothing reads.
  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  // Read after the sleep screen has painted, so the passes it just spent are counted,
  // and written with the state the next boot reads back.
  persistAntiGhostBudget();

  // The per-mode totals for the session, written last so the file ends with the summary
  // of everything above it.
  logPerfSummary();
  PerfLog::flush();
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

void setupDisplayAndFonts(bool seamless = false) {
  display.begin(seamless);
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(VOLLKORN_14_FONT_ID, vollkorn14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(VOLLKORN_12_FONT_ID, vollkorn12FontFamily);
  renderer.insertFont(VOLLKORN_16_FONT_ID, vollkorn16FontFamily);
  renderer.insertFont(VOLLKORN_18_FONT_ID, vollkorn18FontFamily);
#endif  // OMIT_FONTS
  // Permanent Ubuntu ids (full Latin/Arabic/Hebrew/Vietnamese coverage) for the
  // language-select native-name list and the Arabic/Hebrew UI.
  renderer.insertFont(UBUNTU_10_FONT_ID, ubuntu10FontFamily);
  renderer.insertFont(UBUNTU_12_FONT_ID, ubuntu12FontFamily);
  // Paragraph numbers, both sizes. Digits only in practice, so neither rebinds per
  // language; the reader picks between them per book from ReaderPrefs.
  renderer.insertFont(PARA_NUM_FONT_ID, paragraphNumFontFamily);
  renderer.insertFont(PARA_NUM_2X_FONT_ID, paragraphNum2xFontFamily);
  // Active UI ids (SMALL / UI_10 / UI_12): Cozette by default, Ubuntu for Arabic/Hebrew
  // (honors the persisted SETTINGS.language already loaded at this point).
  bindUiFontsForLanguage(renderer);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

// The book "Open Book on Boot" opens, or empty when the mode is Off (or nothing is
// there to open). Last Book prefers the book the last session had open and falls back
// to the newest entry in Recents, so a cold boot after the reader was closed still has
// a book to return to. Either way the file has to still be on the card: a book deleted
// from a computer would otherwise be driven at on every boot until the crash guard
// caught it.
static std::string pickBootBookPath() {
  switch (SETTINGS.bootBookMode) {
    case CrossPointSettings::BOOT_BOOK_RANDOM:
      return pickRandomRecentBookPath();
    case CrossPointSettings::BOOT_BOOK_LAST: {
      if (!APP_STATE.openEpubPath.empty() && Storage.exists(APP_STATE.openEpubPath.c_str())) {
        return APP_STATE.openEpubPath;
      }
      for (const auto& book : RECENT_BOOKS.getBooks()) {
        if (!RecentBooksStore::isMissing(book)) return book.path;
      }
      return "";
    }
    default:
      return "";
  }
}

// A book to open when "Open Book on Boot" is set to Random: any recent entry whose file
// is still on the card. Missing ones are skipped rather than opened and failed.
static std::string pickRandomRecentBookPath() {
  const auto& books = RECENT_BOOKS.getBooks();
  std::vector<const std::string*> candidates;
  candidates.reserve(books.size());
  for (const auto& book : books) {
    if (!RecentBooksStore::isMissing(book)) candidates.push_back(&book.path);
  }
  if (candidates.empty()) return "";
  return *candidates[esp_random() % candidates.size()];
}

void setup() {
  BoardConfig::holdPowerRails();

  t1 = millis();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  //
  // Not paid on a deep-sleep wake. That is the path a reader takes every time it is
  // unlocked, several times an hour, and it was 250 ms of a measured ~2400 ms wake spent
  // waiting for a host that is usually not there: the device is on battery in someone's
  // hands. Every case where a developer IS attached still pays it — a fresh flash, a
  // power-on with the cable in, a panic reboot — because none of those are deep-sleep
  // wakes. The cost of being wrong is log lines missing from an unlock nobody is
  // watching, and replugging brings them back.
  if (esp_reset_reason() != ESP_RST_DEEPSLEEP) delay(250);
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  // checkPanic() clears the watchdog capture marker after a successful SD dump,
  // so isRebootFromPanic() stops answering true partway through setup(). Latch
  // the boot classification here, before that happens, and use it everywhere
  // below.
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  WakeTiming::beginWake();

  gpio.begin();
  // When the ADC button ladder came up. The recovery-combo check below needs the ladder to
  // have settled, and "settled" is time since this call, not time since that check is
  // reached — see the deadline there.
  const unsigned long inputStartedMs = millis();
  powerManager.begin();
  halClock.begin();
  WakeTiming::mark(WakeTiming::Stage::HalReady);

  // Light-sleep through the render task's e-ink BUSY wait (0.3-2 s of pure pin
  // polling) in short slices, waking exactly on the BUSY pin's completion level
  // (falls back to plain polling when WiFi/USB blocks light sleep)
  display.setBusyWaitSliceHook(
      [](int8_t busyPin, uint8_t busyLevel) { return powerManager.onEinkBusyWaitSlice(busyPin, busyLevel); });

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::REGULAR);
    return;
  }

  WakeTiming::mark(WakeTiming::Stage::SdReady);
  // Neither the perf sink nor the wake-timing card read can run here any more: both are
  // switched by a setting, and settings have not been loaded yet. Both start immediately
  // after they are (Stage::ConfigReady), which is still well before the unlock banners
  // that display the numbers.

  HalSystem::checkPanic();

  // Lector: on first install (fresh SD) make sure the folders lector uses exist,
  // so the user can drop files straight in (over WiFi or a card reader) without
  // creating them by hand. ensureDirectoryExists is a quiet no-op when present.
  //   /read        - opened books moved here (CrossPoint "move to read" folder)
  //   /recents     - opened books moved here (lector "move to Recents")
  //   /sleep       - sleep / lock wallpapers (.bmp / .pxc)
  //   /sleep pause - wallpapers paused out of the rotation (note the space)
  {
    static constexpr const char* kLectorFolders[] = {"/read", "/recents", "/sleep", "/sleep pause"};
    for (const char* folder : kLectorFolders) {
      Storage.ensureDirectoryExists(folder);
    }
  }

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  // Restore the anti-ghost budget the last session spent. Without this the count starts
  // at zero on every wake — and since waking is a chip reset, that is every lock — so a
  // device used in short sessions never reaches the full discharge and ghosts forever.
  display.seedFastRefreshesSinceFull(APP_STATE.fastRefreshesSinceFull);
  display.seedInkDebt(APP_STATE.inkDebt);
  RECENT_BOOKS.loadFromFile();
  // One-time upgrade: books read before the reading badges existed have a percentage in
  // the recents list and no marker beside their cache. Seeding costs at most thirteen
  // small writes and only ever happens once per card.
  if (!APP_STATE.readingBadgesSeeded) {
    book_progress::backfillFromRecents();
    APP_STATE.readingBadgesSeeded = true;
    APP_STATE.saveToFile();
  }
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  // Adds the shipped library entry on a card that has never seen it, so it is there
  // to fill credentials into rather than typed out on a five-button keyboard.
  OPDS_STORE.seedBuiltInServers();
  READER_PRESETS.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  WakeTiming::mark(WakeTiming::Stage::ConfigReady);

  // Settings are up, so the timings setting can be honoured. Started before the panel is
  // constructed, so the very first refresh of the session is recorded rather than missed,
  // and the previous wake's stage breakdown is appended straight after the header so one
  // copied file carries both the wake cost and the refresh costs that follow it.
  startPerfLogSink(gpio.deviceIsX3() ? "x3" : "x4");
  WakeTiming::setEnabled(SETTINGS.showTimings != 0);
  WakeTiming::loadPrevious();
  logWakeTimingToPerfLog();

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      if (!gpio.verifyPowerButtonWakeup(SETTINGS.getWakeHoldMs(), SETTINGS.wakeHoldIsFast())) {
        powerManager.startDeepSleep(gpio);
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // Recovery firmware mode: hold left side button (BTN_UP) together with the power button at
  // boot to skip directly to the SD-card firmware update screen. Useful on devices where USB
  // flashing has been locked down (e.g. recent X3 firmware).
  bool recoveryFirmwareMode = false;
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    // Refresh the cached button state until the ADC button ladder has settled — isPressed()
    // needs ~half a second after the ladder comes up, per the HalGPIO contract.
    //
    // The deadline runs from gpio.begin(), not from here. It used to be a flat 500 ms
    // measured from this point, which meant the settle window began only after the card
    // mount and the settings load had already given the ladder a few hundred milliseconds
    // of their own. Every wake therefore paid the full half second twice over, once
    // implicitly and once on the clock, and a measured X3 wake spent 550 ms of ~2400 ms
    // inside this loop. Anchoring to when the ladder actually started keeps the settle
    // window the same length in absolute terms and stops charging for it a second time.
    //
    // The loop also stops the moment UP reads pressed: once the answer is yes, waiting
    // longer cannot change it. A held combo is therefore detected at least as reliably as
    // before, never less.
    constexpr unsigned long kInputSettleMs = 500;
    while (millis() - inputStartedMs < kInputSettleMs) {
      gpio.update();
      if (gpio.isPressed(HalGPIO::BTN_UP)) break;
      delay(10);
    }
    gpio.update();
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  WakeTiming::mark(WakeTiming::Stage::InputSettled);

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const BootResume resume = isSilentReboot                             ? BootResume::Silent
                            : isSleepWake && !APP_STATE.showBootScreen ? BootResume::SplashlessWake
                                                                       : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

  // "Open Book on Boot": pick the target BEFORE the unlock banners paint, so the
  // banner names the book this boot is about to open instead of the previous one. The
  // result is reused by the routing block below — picking twice would name one book and
  // open another. Guarded by every condition that block uses and that is already known
  // here, so a boot heading to recovery, the crash report, or a silent-reboot target never
  // advertises a book it will not open. Back-held and readerActivityLoadCount are the
  // routing block's own escape hatches and are re-checked there.
  //
  // Quick Resume opts out of the whole idea: it opens nothing new, so it must not pick a
  // random book either. The Light face already picked one at lock time (it named the book
  // on the sleep screen), and that stored path is reused here rather than re-rolled.
  //
  // Both are only honoured on an actual button wake. A cold boot (battery pulled, USB
  // power, a flash) leaves the same fields behind in state.json, and a device that was
  // powered off has not asked to be put back into a book.
  const bool sleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool quickResumeWake = sleepWake && APP_STATE.quickResumeWake && resume == BootResume::SplashlessWake;
  const bool quickResumeTargetIsReader = APP_STATE.quickResumeTargetIsReader;
  const std::string pendingWakeBookPath = sleepWake ? APP_STATE.pendingWakeBookPath : std::string();

  std::string bootBookPath;
  if (!quickResumeWake && !pendingWakeBookPath.empty()) {
    bootBookPath = pendingWakeBookPath;
    setUnlockBannerBookPath(bootBookPath);
  } else if (!quickResumeWake && SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF && !recoveryFirmwareMode &&
             !rebootedFromPanic && resume != BootResume::Silent && APP_STATE.readerActivityLoadCount == 0 &&
             !mappedInputManager.isPressed(MappedInputManager::Button::Back)) {
    bootBookPath = pickBootBookPath();
    if (!bootBookPath.empty()) setUnlockBannerBookPath(bootBookPath);
  }

  // Waking from a wallpaper sleep face: a normal (non-quick-resume) deep-sleep wake whose
  // sleep screen was a custom wallpaper. The unlock path below blanks the panel and goes
  // to the book instead of showing the boot splash. Needs the seamless begin() so the
  // panel keeps the wallpaper (no clearing pass) until that blank lands.
  //
  // Every wallpaper format qualifies, not just .pxc. The old .pxc-only rule existed
  // because the wake re-rendered the image, and .pxc was the one format with a render
  // path fast enough to attempt on a wake. Nothing is re-rendered now, so a BMP sleep
  // face takes the same fast unlock a .pxc one does.
  const std::string& lastWallpaper = APP_STATE.lastSleepWallpaperPath;
  const bool sleepWasCustomWallpaper =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
      (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM && !APP_STATE.lastSleepFromReader);
  const bool wallpaperWake = resume == BootResume::Splash && wakeupReason == HalGPIO::WakeupReason::PowerButton &&
                             sleepWasCustomWallpaper && !lastWallpaper.empty();

  setupDisplayAndFonts(resume != BootResume::Splash || wallpaperWake);
  WakeTiming::mark(WakeTiming::Stage::DisplayReady);

  // The wake/unlock banners are the first thing seen on waking, and the activity that
  // follows repaints straight over them. They are the loading face: they exist to show the
  // reader is busy while input is still gated, not to be read.
  //
  // They do hold a floor of UNLOCK_BANNER_MIN_VISIBLE_MS, so the book title is readable
  // rather than a flicker. It is a floor and not a delay: a wake whose next paint is
  // already later than that pays nothing, which on a cold book open is every wake.
  //
  // Quick Resume draws no banners at all. Its whole promise is that unlocking changes
  // nothing on the panel, and a banner is a change.

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::SplashlessWake: {
      // One-shot flag: re-arm the splash for the next ordinary boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a splashless-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      // exists() first: a missing frame file is the ordinary case for a sleep mode that
      // never saved one, and the check costs less than an open that is going to fail.
      const bool sleepFrameRestored = Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer();
      // Stamped whichever way it went: "the frame was missing" is itself an answer to
      // where the wake's time went, and a stage that is only stamped on success reads as
      // a fast wake when it never ran at all.
      WakeTiming::mark(WakeTiming::Stage::FrameLoaded);
      if (sleepFrameRestored) {
        // Frame restored: draw the wake/unlock banners over the retained wallpaper
        // (version + resuming book on top, custom footer on the bottom), matching old
        // lector. The banners are the loading face; input stays gated until the reader
        // paints, so this does not change the "no phantom clicks" wake behavior.
        //
        // Upstream paints a loading icon here instead. The face is ours; the refresh
        // path below is upstream's (#2698) and is what stops the X3 flashing on the way
        // in — only the banner pixels change, so only they are driven.
        const bool useDifferentialRefresh = gpio.deviceIsX3();
        if (useDifferentialRefresh) {
          // begin() clears the X3 controller RAM, so restore the saved frame as
          // the baseline before drawing the banners over it.
          renderer.cleanupGrayscaleWithFrameBuffer();
        }
        WakeTiming::mark(WakeTiming::Stage::BaselineRestored);
        const bool bannersDrawn = !quickResumeWake;
        if (bannersDrawn) drawUnlockBanners(renderer);
        WakeTiming::mark(WakeTiming::Stage::BannersDrawn);
        if (useDifferentialRefresh) {
          renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
          // The panel already holds the page; the reader's first paint can go over it.
          allowFastInitialReaderRefresh = true;
        } else {
          renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        }
        // Stamped after the push: the floor measures how long the banners are on the
        // glass, not how long ago they were drawn into a buffer.
        if (bannersDrawn) unlockBannersShownAt = millis();
      } else if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM) {
        activityManager.goToBoot();  // frame file missing, fall back to the splash
      } else {
        // Custom sleep face with no saved frame: nothing paints here, so the panel still
        // physically shows the sleep image. Tell the first Home paint to clear it with one
        // HALF_REFRESH instead of leaving the image under the menu (upstream #3009).
        needsWakeRefresh = true;
      }
      break;
    }
    case BootResume::Splash:
      // Waking from a wallpaper sleep face never redraws the wallpaper. The sleep screen
      // itself is untouched — the wallpaper is still what the panel shows all night — but
      // the unlock does not decode it a second time. Re-reading the .pxc and re-dithering
      // 384,000 pixels measured at 3.3-3.7s of a ~4.7s wake on an X3 (lector.exp.9), and
      // every one of those pixels is covered by the book page moments later.
      //
      // So: blank the framebuffer, draw the unlock banners into it if they are wanted,
      // and put that up with one FULL pass.
      //
      // The blank is not optional and not decoration. A wallpaper is arbitrary content,
      // and a differential waveform only drives the pixels that changed — paint a page
      // straight over a wallpaper and the wallpaper stays in the page, which is exactly
      // what the first build of this path did (device photo, 0.15.0). FAST_REFRESH cannot
      // stand in for FULL_REFRESH here either: its custom LUT nudges changed pixels with a
      // short waveform and does not reset the ink, so dark wallpaper survives it. Only the
      // complete waveform over a blank buffer truly clears the panel, and only then may
      // the reader's own first paint take the cheap differential path.
      //
      // The blank also doubles as the loading face when the banners are off: the screen
      // goes blank the moment the wake starts, while the button is still held, so there IS
      // a visible answer to the press before the page arrives.
      if (wallpaperWake) {
        bool bannersDrawn = false;
        renderer.clearScreen();
        if (!SETTINGS.wakeStraightToBook) {
          // Banners wanted: they now sit on a blank page instead of over the wallpaper.
          // They cost only the draw — the FULL pass below happens either way.
          drawUnlockBanners(renderer);
          bannersDrawn = true;
        }
        renderer.displayBuffer(HalDisplay::FULL_REFRESH);
        if (bannersDrawn) unlockBannersShownAt = millis();
        allowFastInitialReaderRefresh = true;
        break;
      }
      // goToBoot() runs BootActivity::onEnter inline (no current activity yet), and that
      // paint is blocking, so the splash is already on the panel when this returns.
      activityManager.goToBoot();
      break;
  }

  WakeTiming::mark(WakeTiming::Stage::BannersUp);

  // Wallpaper index reconcile. A battery lock on the Xteink boards is a full
  // power cut (battery latch), so every unlock arrives as ESP_RST_POWERON —
  // reset reason alone cannot separate "wake" from "the card was out". The
  // walk therefore runs only when something says the folder changed: a
  // persisted dirty mark (WiFi file browser, pause moves, deletes), the pick's
  // needs-rebuild flag, or the millisecond folder probe seeing the last live
  // directory slot move or the folder's own timestamp change (either one means
  // files were written from a computer). A clean unlock pays the probe and
  // skips the walk — no banner, no folder scan. A plain software
  // restart with no dirty mark (a settings-only WiFi session, OTA) skips the
  // folder probe: the folder's contents cannot change behind a running device
  // except through the hooked paths. Which folder to read can still change
  // across an update, so that one check runs on every boot.
  {
    const esp_reset_reason_t rst = esp_reset_reason();
    const bool wantsWallpaperIndex = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
                                     SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM;
    if (rst != ESP_RST_DEEPSLEEP && !recoveryFirmwareMode && !rebootedFromPanic && wantsWallpaperIndex) {
      if (APP_STATE.sleepIndexDirty || APP_STATE.sleepIndexNeedsRebuild ||
          crosspoint::sleep::windex::indexedFolderChanged() ||
          (rst != ESP_RST_SW && crosspoint::sleep::windex::folderLooksChanged())) {
        crosspoint::sleep::windex::reconcileAtColdBoot(renderer);
      }
    }
  }

  // Where this wake is allowed to land. Quick Resume and the Light face both override
  // the ordinary conditions below; every other boot resolves to Unchanged and falls
  // through untouched. Recovery, a panic report and a silent reboot are handled before
  // this and keep their own targets.
  //
  // The Light face's book is the one it named on the sleep screen; Quick Resume returns
  // to the book that was open when it locked.
  const std::string forcedBookPath = quickResumeWake                ? APP_STATE.openEpubPath
                                     : !pendingWakeBookPath.empty() ? pendingWakeBookPath
                                                                    : APP_STATE.openEpubPath;
  wake_route::WakeInputs wakeInputs;
  wakeInputs.quickResume = quickResumeWake;
  wakeInputs.quickResumeTargetIsReader = quickResumeTargetIsReader;
  wakeInputs.forceBookOnWake = !quickResumeWake && !pendingWakeBookPath.empty();
  wakeInputs.hasBook = !forcedBookPath.empty();
  wakeInputs.sleptFromReader = APP_STATE.lastSleepFromReader;
  wakeInputs.backHeld = mappedInputManager.isPressed(MappedInputManager::Button::Back);
  wakeInputs.bookOnBoot = SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF;
  wakeInputs.readerCrashed = APP_STATE.readerActivityLoadCount > 0;
  const wake_route::Route forcedRoute = (recoveryFirmwareMode || rebootedFromPanic || resume == BootResume::Silent)
                                            ? wake_route::Route::Unchanged
                                            : wake_route::resolve(wakeInputs);

  // One-shot: consumed by this wake, so a later ordinary boot routes normally.
  if (APP_STATE.quickResumeWake || APP_STATE.quickResumeTargetIsReader || !APP_STATE.pendingWakeBookPath.empty()) {
    APP_STATE.quickResumeWake = false;
    APP_STATE.quickResumeTargetIsReader = false;
    APP_STATE.pendingWakeBookPath.clear();
    APP_STATE.saveToFile();
  }

  // The banners have had the panel to themselves up to here; hold them for their floor
  // before the activity below paints over them.
  waitForUnlockBannerFloor();

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (rebootedFromPanic) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (forcedRoute == wake_route::Route::ForceReader) {
    // Quick Resume returning to its book, or the Light face opening the book it named on
    // the sleep screen. Both bypass the ordinary conditions below on purpose: a held Back
    // and "last sleep was not from the reader" are answers to questions neither face asks.
    const auto path = forcedBookPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  } else if (forcedRoute == wake_route::Route::ForceHome) {
    // Quick Resume that locked on a screen the reset cannot rebuild (home was painted
    // before the moon, so this IS the picture on the glass), or either face hitting a
    // safety valve: no book to open, or a reader that crashed last boot.
    activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
  } else if (SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF || APP_STATE.openEpubPath.empty() ||
             !APP_STATE.lastSleepFromReader || mappedInputManager.isPressed(MappedInputManager::Button::Back) ||
             APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    //
    // "Open Book on Boot" jumps straight into a book instead: the last-read one, or one
    // of the books in progress at random.
    // Skipped when Back is held (the user is asking for home) or after a reader crash,
    // so a book that cannot open can never wedge boot.
    // bootBookPath was chosen above, before the banners painted, so the banner named
    // this exact book. Back held or a prior reader crash clears it rather than re-picking.
    const bool backHeld = mappedInputManager.isPressed(MappedInputManager::Button::Back);
    if (backHeld || APP_STATE.readerActivityLoadCount > 0) bootBookPath.clear();
    if (!bootBookPath.empty()) {
      // Same crash-loop guard the resume path uses: bump the counter first so a crash
      // while opening lands on home next boot instead of trying again forever.
      APP_STATE.readerActivityLoadCount++;
      APP_STATE.saveToFile();
      activityManager.goToReader(bootBookPath);
    } else {
      activityManager.goHome(HomeMenuItem::NONE, needsWakeRefresh);
    }
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  WakeTiming::mark(WakeTiming::Stage::ActivityUp);
  // Last stamp taken, so the record is complete. Written here rather than at sleep entry:
  // the reader can be powered off from any screen, and a wake that is never followed by a
  // clean sleep would otherwise report nothing.
  WakeTiming::persist();

  if (resume == BootResume::Silent) {
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  allowSleepAt = millis() + 2000;
}

// delay() counts ticks, and the tick stops while onEinkBusyWaitSlice() light-sleeps
// the chip (millis() is RTC-corrected on wake; the tick is not). A delay(10) mid-refresh
// would stretch to ~210 ms and starve button sampling. millis() stays honest.
static void delayWallClock(const unsigned long ms) {
  const unsigned long deadline = millis() + ms;
  while (static_cast<long>(millis() - deadline) < 0) {
    vTaskDelay(1);
  }
}

// Polls the battery and shows the low-battery notice the first time the charge drops to
// the warning level. The rule itself lives in low_battery::resolve(), so the thresholds,
// the hysteresis and the "no usable reading" case are covered by host tests.
//
// Polled on a slow timer rather than every pass: on a board with no fuel gauge each
// reading is a fresh ADC sample plus smoothing, which is not worth paying at loop rate
// for a value that moves over hours. The first poll waits out that interval too, so the
// reading is a settled one rather than whatever the first sample after a boot says.
//
// The notice is pushed (not a replace) and only over the reader or home, so a firmware
// update, a web transfer or the sleep screen is never interrupted by it. The "already
// warned" latch is written by the notice itself, once it is genuinely on screen: writing
// it here would lose the warning entirely if the activity that runs between this call and
// the pending push replaced the pushed activity with one of its own.
static void checkLowBatteryWarning() {
  constexpr unsigned long POLL_INTERVAL_MS = 60000;
  static unsigned long lastCheckMs = 0;

  const unsigned long now = millis();
  if (lastCheckMs == 0) lastCheckMs = now;  // first poll is one interval away, not immediate
  if ((now - lastCheckMs) < POLL_INTERVAL_MS) return;
  lastCheckMs = now;

  low_battery::Inputs inputs;
  inputs.percent = powerManager.getBatteryPercentage();
  inputs.alreadyWarned = APP_STATE.lowBatteryWarned;
  inputs.usbConnected = gpio.isUsbConnectedCached();
  inputs.screenAllowed = activityManager.isReaderActivity() || activityManager.isHomeActivity();

  switch (low_battery::resolve(inputs)) {
    case low_battery::Action::ClearLatch:
      APP_STATE.lowBatteryWarned = false;
      APP_STATE.saveToFile();
      break;
    case low_battery::Action::Warn:
      // Night mode inverts the reading surface only, so the notice has to follow whatever
      // it was pushed over or the panel flips polarity around it.
      activityManager.pushActivity(std::make_unique<LowBatteryNoticeActivity>(
          renderer, mappedInputManager, inputs.percent, activityManager.isReaderActivity()));
      break;
    case low_battery::Action::None:
    default:
      break;
  }
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPressSleeps());
  gpio.update();

  // Cleared here, once, so that every early return below (the screenshot combo, the sleep
  // paths) leaves the power release ungated. The double-click block further down is the
  // only thing that ever sets it, and only for the pass that sets it.
  mappedInputManager.setPowerReleaseOverride(false, false);

  renderer.setFadingFix(SETTINGS.fadingFix);
  // Read every pass, like the fading fix above, so toggling the setting takes effect on
  // the next paint instead of on the next boot.
  renderer.setTimingOverlay(SETTINGS.showTimings != 0, UI_10_FONT_ID);
  display.setFastPageTurns(SETTINGS.fastPageTurns != 0);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }
  // The press, not the release, and not "any activity": this is the instant the reader's
  // thumb acted, and the refresh that answers it closes the measurement. A release-driven
  // action (a short power click) still lands within the same press-to-paint window.
  if (gpio.wasAnyPressed()) PerfStats::noteInput(millis());

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // A hold that woke the device must be released before it can count as a new
  // in-app long press. Otherwise a user who keeps holding after wake would put
  // the device straight back to sleep once allowSleepAt expires.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  if (powerReleasedSinceWake && millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getSleepHoldMs()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Power double-click, EPUB reader only.
  //
  // Sits below the screenshot combo and both sleep checks, so holding to sleep and
  // Power+Down still win outright, and above every consumer of a power release, so a click
  // is held back before anything acts on it. Nothing can tell a single click from the first
  // half of a double click until the window closes, which is why a click is delayed rather
  // than acted on and undone.
  //
  // The detector is static: it must survive between loop passes. It is reset whenever the
  // feature is not armed, so a click pending when the book closes cannot fire into the
  // screen that replaced it.
  {
    static reader_input::DoubleClickDetector powerClicks;
    if (!activityManager.wantsPowerDoubleClick()) {
      powerClicks.reset();
    } else {
      // The RAW edge, deliberately: mappedInputManager.wasReleased() is what the override
      // below rewrites, and feeding the detector its own output would latch it.
      const bool released = gpio.wasReleased(HalGPIO::BTN_POWER);
      const auto event = powerClicks.update(released, millis());
      if (event == reader_input::DoubleClickDetector::Event::Double) {
        activityManager.runPowerDoubleClick();
      }
      // Hide the release while the verdict is pending (and on the pass the verdict is
      // Double, whose second edge belongs to the double click); replay it on the pass the
      // verdict is Single, where every consumer below sees the edge it always saw.
      mappedInputManager.setPowerReleaseOverride(
          powerClicks.waiting() || event == reader_input::DoubleClickDetector::Event::Double,
          event == reader_input::DoubleClickDetector::Event::Single);
    }
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  checkLowBatteryWarning();

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

  // Body complete: releases the slice hook's yield (see onEinkBusyWaitSlice).
  powerManager.noteMainLoopIteration();

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    const unsigned long idleMs = millis() - lastActivityTime;
    // Drop the panel's rails before the chip starts light-sleeping.
    //
    // The image is bistable and needs no power to stay put, but the paint path never asked
    // the controller to power down (every displayBuffer/refreshDisplay call in this fork
    // takes the default turnOffScreen=false), so the rails stayed live from the first paint
    // until the device slept. A powered controller with no waveform running keeps a weak
    // bias on the pixels, and left alone the image drifts into speckle — reported on an X3
    // as a clean screen that degraded within seconds of being put down, with no input.
    //
    // This costs nothing visible: no flash, no waveform, and the next paint brings the
    // rails back up by itself. Repeat calls are free, so no "already off" flag is kept
    // here — the driver holds that state and returns immediately when it is already down.
    if (idleMs >= HalPowerManager::IDLE_PANEL_POWER_OFF_MS) display.powerOffPanel();
    if (idleMs >= HalPowerManager::IDLE_LIGHT_SLEEP_MS) {
      // Idle: light-sleep between input polls instead of busy-delaying (same poll cadence).
      // Race-to-sleep: run the brief wake windows at normal clock, not LOW_POWER_FREQ.
      // The board's sleep-floor current is paid per-millisecond regardless of CPU
      // speed, so finishing the per-wake work ~16x faster and returning to sleep
      // costs less charge than stretching the window at 10 MHz (measured at 10 MHz:
      // 8.8 mA for 4.5 ms per wake). The downclock below only serves the pre-sleep
      // 100 Hz delay-poll phase. The lightSleep()-rejected fallback delay() then
      // also runs at normal clock, but that only happens when USB (externally
      // powered), WiFi, or a render Lock (full speed wanted anyway) is active.
      powerManager.setPowerSaving(false);
      if (gpio.isDebouncePending()) {
        // A raw button-state change is mid-debounce: commitment needs a second
        // matching sample, so poll again quickly instead of sleeping a slice —
        // a tap shorter than the 50 ms cadence would otherwise land in a single
        // sample and be dropped, and every press would commit a slice late.
        delayWallClock(10);
      } else if (!powerManager.lightSleep(gpio)) {
        // Light sleep declined = a render Lock, USB, or WiFi is active — the
        // chip is at full clock anyway, so poll at 100 Hz. A 50 ms cadence
        // here dropped sub-slice power taps (a press needs two samples >=5 ms
        // apart to commit), which made short-press sleep flaky during renders
        // — exactly when a render Lock forces this fallback.
        delayWallClock(10);
      }
    } else {
      // Response window after recent input: keep 100 Hz polling for snappy interaction,
      // but downclock once rapid-input bursts have settled — renders re-raise the clock
      // via HalPowerManager::Lock, so full speed only serves loop bookkeeping here
      if (idleMs >= HalPowerManager::IDLE_DOWNCLOCK_MS) {
        powerManager.setPowerSaving(true);
      }
      delayWallClock(10);
    }
  }
}
