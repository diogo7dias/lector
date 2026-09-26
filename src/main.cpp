#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
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
#include "Diagnostics.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "PerfLogSink.h"
#include "ReaderPresetStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SleepTiming.h"
#include "UiFont.h"
#include "WakeTiming.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/PxcSleepRenderer.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/util/LowBatteryNoticeActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "frontlight/FrontlightBootPolicy.h"
#include "network/FirmwareSwitchAudit.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WakeFacePolicy.h"
#include "sleep/WakeRoutePolicy.h"
#include "sleep/WakeSequence.h"
#include "util/BookProgressFile.h"
#include "util/ButtonNavigator.h"
#include "util/ButtonReplay.h"
#include "util/ButtonRouter.h"
#include "util/DebugTrace.h"
#include "util/DoubleClickDetector.h"
#include "util/LowBatteryPolicy.h"
#include "util/ScreenshotUtil.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
// ChareInk is lector's single built-in reading family (an e-ink tuned Charis derivative).
// It is compiled in at 14 pt only: its glyph set is far larger than the Vollkorn it
// replaced, and four sizes would not fit the app partition. Every other size, and every
// other family, is installed from the SD card. Noto Sans survives only as the 8 pt small
// font (below) and Ubuntu as the UI font.
EpdFont chareink14RegularFont(&chareink_14_regular);
EpdFont chareink14BoldFont(&chareink_14_bold);
EpdFont chareink14ItalicFont(&chareink_14_italic);
EpdFont chareink14BoldItalicFont(&chareink_14_bolditalic);
EpdFontFamily chareink14FontFamily(&chareink14RegularFont, &chareink14BoldFont, &chareink14ItalicFont,
                                   &chareink14BoldItalicFont);

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
  Splash,  // cold boot, flash, panic, or plain reboot
  Silent,  // heap-defrag ESP.restart() (RTC flag; lost on power loss)
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

// Defined below setup()'s helpers.
static std::string pickRandomRecentBookPath();
static std::string pickBootBookPath();

void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  // Lock cost, stage by stage. The wake side has had WakeTiming since the boot path was
  // first measured; the sleep side had nothing, so "locking feels slow" could not be
  // answered with a number. Same split idea: each stamp is taken after the step it names,
  // and the report prints the difference between neighbours.
  const unsigned long sleepT0 = millis();
  SleepTiming::begin();
  unsigned long sleepTState = sleepT0;
  unsigned long sleepTPaint = sleepT0;
  unsigned long sleepTFrame = sleepT0;
  unsigned long sleepTWifi = sleepT0;
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  // Only the retired crest face ever named the wake's book on the sleep screen; no face
  // sets this any more, and a stale one from an older build must not force a wake.
  APP_STATE.pendingWakeBookPath.clear();

  // ponytail: no save here. persistAntiGhostBudget() below writes APP_STATE after the paint,
  // and a JSON rewrite per lock stage was two redundant SD writes on every lock.
  sleepTState = millis();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);
  sleepTPaint = millis();

  sleepTFrame = millis();

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  sleepTWifi = millis();

  // Read after the sleep screen has painted, so the passes it just spent are counted,
  // and written with the state the next boot reads back. This is the lock's only
  // APP_STATE write: everything set above and in SleepActivity lands with it.
  persistAntiGhostBudget();

  // The per-mode totals for the session, written last so the file ends with the summary
  // of everything above it.
  logPerfSummary();

  // Written before the flush, so the stages land in the same CSV as the refreshes they
  // paid for. The panel power-down and the total are serial-only: the file is closed by
  // then, and a kit log is where those two get read anyway.
  const unsigned long sleepTBudget = millis();
  // The paint stage is the one worth breaking down: it was 10112 ms of a 10258 ms lock
  // when the stages were first measured, and only about 4800 ms of that was the panel.
  char paintStages[320];
  SleepTiming::format(paintStages, sizeof(paintStages));
  char sleepNote[448];
  snprintf(sleepNote, sizeof(sleepNote), "sleep state=%lu paint=%lu frame=%lu wifi=%lu save=%lu [%s]",
           sleepTState - sleepT0, sleepTPaint - sleepTState, sleepTFrame - sleepTPaint, sleepTWifi - sleepTFrame,
           sleepTBudget - sleepTWifi, paintStages);
  PerfLog::note(sleepNote);
  PerfLog::flush();
  // Ordinarily empty: attempts and failures flush as they happen. This is the
  // safety net for anything recorded since, before the card loses power.
  diag::flushIfPending();

  display.deepSleep();
  Storage.prepareForDeepSleep();
  const unsigned long sleepTPanel = millis();
  // INF, not DBG: a release kit runs at LOG_LEVEL 1 and this is the one line that says
  // what a lock cost.
  LOG_INF("SLP", "Lock %lu ms total (%s panel=%lu)", sleepTPanel - sleepT0, sleepNote, sleepTPanel - sleepTBudget);
  LOG_DBG("MAIN", "Entering deep sleep");
  // Last chance: startDeepSleep() does not return, and the USB-CDC link dies with the
  // chip, so anything still buffered here is lost.
  logFlush();

  powerManager.startDeepSleep(gpio);
}

// Recovery firmware chord: a side button held together with the power button at
// boot. BTN_UP everywhere except the X4 Pro, whose BTN_UP sits on GPIO0, the
// boot-strap pin: holding that at boot drops the chip into the ROM bootloader
// instead of into our firmware, so the chord uses BTN_DOWN there.
//
// `inputStartedMs` is when gpio.begin() ran; the settle window is measured from
// there, not from the call, so the work done in between counts towards it.
//
// TWO AGREEING SAMPLES, not one. The buttons are a resistor ladder read as a
// voltage on a single analog input, and the panel rails come up on the same
// supply. A single nudged sample must not be able to strand a wake in the
// recovery picker: that screen deliberately has no way back to the reader, so a
// false positive costs a power cycle. Two samples at least 5 ms apart is what
// HalGPIO's own debounce requires to commit a press.
//
// None of that ladder reasoning applies to the X4 Pro: its side buttons are
// plain debounced digital inputs that read true almost immediately, so the
// settle window is 20 ms there instead of 500 (100 with Fast Unlock).
bool recoveryChordHeld(const unsigned long inputStartedMs) {
  const bool isX4Pro = BoardConfig::isX4Pro();
  // 500 ms stock, 100 ms with Fast Unlock; see wake_face::inputSettleMs. Settings are
  // loaded before either caller reaches this, except the no-card recovery path, where
  // the default (fast) applies.
  const unsigned long inputSettleMs = wake_face::inputSettleMs(isX4Pro, SETTINGS.fastUnlock != 0);
  const uint8_t chordButton = isX4Pro ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
  const auto chordConfirmed = [chordButton] {
    gpio.update();
    if (!gpio.isPressed(chordButton)) return false;
    delay(6);
    gpio.update();
    return gpio.isPressed(chordButton);
  };
  // The loop stops the moment the chord is confirmed: once the answer is yes,
  // waiting longer cannot change it.
  while (millis() - inputStartedMs < inputSettleMs) {
    if (chordConfirmed()) return true;
    delay(10);
  }
  return chordConfirmed();
}

static void setupDisplay(const bool seamless, const HalGPIO::WakeupReason wakeupReason) {
  display.begin(seamless, wakeupReason);
  renderer.begin();
  // Only this file can put the device down, so the light panel's Sleep button is handed
  // the same entry point every other sleep route uses.
  activityManager.setSleepAction([] { enterDeepSleep(); });
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");
}

// The built-in families only. SD card families are sdFontSystem.begin(), kept apart so a
// wake can start its panel work between the two and let the card read overlap it.
static void setupBuiltinFonts() {
  // Initialize font decompressor for compressed reader fonts
  if (!fontDecompressor.init()) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(CHAREINK_14_FONT_ID, chareink14FontFamily);
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
}

void setupDisplayAndFonts(const bool seamless, const HalGPIO::WakeupReason wakeupReason) {
  setupDisplay(seamless, wakeupReason);
  setupBuiltinFonts();
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
  // First thing of all: the earliest stamp has to be able to land, and this only zeroes
  // two small arrays.
  WakeTiming::beginWake();
  BoardConfig::holdPowerRails();

  t1 = millis();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  //
  // Not paid on a wake. That is the path a reader takes every time it is unlocked,
  // several times an hour, and it was 250 ms of a measured ~2400 ms wake spent waiting
  // for a host that is usually not there: the device is on battery in someone's hands.
  // A wake is a deep-sleep reset on the X3 and the X4 Pro but a POWERON on the X4 (see
  // lib/hal/WakeClassify.h), so every X4 unlock used to pay this. The full classifier
  // cannot answer here: it needs the board and USB state, and gpio is not up yet.
  // A POWERON with the cable in is a charge-sleep boot (HalGPIO::getWakeupReason, AfterUSBPower) that prints one
  // line and sleeps again. A fresh flash (RST_UNKNOWN), a panic reboot and a software
  // restart still pay it, because those are the boots a developer is watching. The cost
  // of being wrong is log lines missing from a boot nobody is watching, and replugging
  // brings them back.
  {
    const esp_reset_reason_t rst = esp_reset_reason();
    if (rst != ESP_RST_DEEPSLEEP && rst != ESP_RST_POWERON) delay(250);
  }
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  // First stamp of the wake, and deliberately outside the serial block above: a build
  // without ENABLE_SERIAL_LOG must still land stamp 0, because every other stage is a
  // delta from it and the readout prints nothing at all without it. Everything before
  // this point is the framework's own startup plus, when it is compiled in, serial
  // bring-up. Neither is ours to shorten.
  WakeTiming::mark(WakeTiming::Stage::SerialUp);

  HalSystem::begin();
  WakeTiming::mark(WakeTiming::Stage::SysReady);
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  // When the ADC button ladder came up. The recovery-combo check below needs the ladder to
  // have settled, and "settled" is time since this call, not time since that check is
  // reached — see the deadline there.
  const unsigned long inputStartedMs = millis();
  WakeTiming::mark(WakeTiming::Stage::GpioReady);
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
  bool sdRecoveryChord = false;
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    diag::recordSdMountFailure();  // reaches the card only if a retry mounts it
    // Classified here because the panel comes up before the main classification below.
    // That one still reads afresh: the retry loop can hold the boot for minutes first.
    const auto earlyWakeupReason = gpio.getWakeupReason();
    setupDisplayAndFonts(isSilentReboot, earlyWakeupReason);
    // The firmware picker lives on the SD card, so a card that will not mount
    // used to end the boot right here -- on a device whose USB flashing the
    // vendor locked, that is the last way off this firmware gone. When the
    // recovery chord is held, keep asking for the card instead of giving up.
    sdRecoveryChord = earlyWakeupReason == HalGPIO::WakeupReason::PowerButton && recoveryChordHeld(inputStartedMs);
    if (!sdRecoveryChord) {
      activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::REGULAR);
      return;
    }
    activityManager.goToFullScreenMessage("Insert an SD card with firmware.bin", EpdFontFamily::REGULAR);
    // Five minutes of retries, not forever: a reader left in a drawer with the
    // chord stuck down should end up asleep rather than polling the card slot
    // until the battery is flat.
    constexpr unsigned long SD_RETRY_WINDOW_MS = 5UL * 60UL * 1000UL;
    const unsigned long retryStartedMs = millis();
    bool mounted = false;
    while (millis() - retryStartedMs < SD_RETRY_WINDOW_MS) {
      delay(1000);
      if (Storage.begin()) {
        mounted = true;
        break;
      }
    }
    if (!mounted) {
      activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::REGULAR);
      return;
    }
    LOG_INF("MAIN", "SD card mounted on retry; entering recovery firmware mode");
  }

  WakeTiming::mark(WakeTiming::Stage::SdReady);
  // Neither the perf sink nor the wake-timing card read can run here any more: both are
  // switched by a setting, and settings have not been loaded yet. Both start immediately
  // after they are (Stage::ConfigReady), before the wake diagnostics are logged.

  HalSystem::checkPanic();

  // If a firmware update handed the device to another image and the bootloader
  // refused it, we are running the old firmware right now and nothing else
  // would say so. Write that to the SD card while it is fresh.
  firmware_flash::auditPendingSwitch(CROSSPOINT_VERSION);
  // A crash, watchdog or brownout boot is recorded too; an ordinary wake is
  // not, so this costs a card write only on the boots worth one.
  diag::recordAbnormalBoot();
  diag::flushIfPending();

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

  const uint32_t configStartedMs = millis();
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
  OPDS_STORE.loadFromFile();
  // Adds the shipped library entry on a card that has never seen it, so it is there
  // to fill credentials into rather than typed out on a five-button keyboard.
  OPDS_STORE.seedBuiltInServers();
  ButtonNavigator::setMappedInputManager(mappedInputManager);
  WakeTiming::noteCost(WakeTiming::Cost::Config, millis() - configStartedMs);
  WakeTiming::mark(WakeTiming::Stage::ConfigReady);

  // Settings are up, so the timings setting can be honoured. Started before the panel is
  // constructed, so the very first refresh of the session is recorded rather than missed,
  // and the previous wake's stage breakdown is appended straight after the header so one
  // copied file carries both the wake cost and the refresh costs that follow it.
  const uint32_t logsStartedMs = millis();
  debug_trace::begin();
  {
    const DeviceProfile dev = deviceProfileFromHardware();
    startPerfLogSink(dev.isX3 ? "x3" : dev.isX4Pro ? "x4pro" : "x4");
  }
  WakeTiming::noteCost(WakeTiming::Cost::Logs, millis() - logsStartedMs);
  WakeTiming::setEnabled(SETTINGS.showTimings != 0);
  WakeTiming::loadPrevious();
  logWakeTimingToPerfLog();
  // Also on serial, so a kit log answers the unlock half without the card having to be
  // read. Empty on the first boot after a flash, when there is no previous wake to report.
  {
    char wakeDiag[176];
    WakeTiming::formatDiagnostic(wakeDiag, sizeof(wakeDiag));
    if (wakeDiag[0] != '\0') LOG_INF("SLP", "Unlock %s", wakeDiag);
  }

  // Brightness and warmth always come back; whether the light itself does is
  // FrontlightBootPolicy's call. Inert on a board without a frontlight.
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth,
                   frontlight::restoreLightOnAtBoot(
                       {SETTINGS.frontlightOn != 0, SETTINGS.frontlightRestoreOnWake != 0, isSilentReboot}));

  const uint32_t classifyStartedMs = millis();
  const auto wakeupReason = gpio.getWakeupReason();
  // INF, and on every boot: a device that sleeps and is woken straight back up by its own
  // USB power prints nothing else, so without this line the cycle can only be inferred
  // from the gaps in a capture. See the AfterUSBPower branch below.
  static constexpr const char* kWakeReasonNames[] = {"PowerButton", "AfterFlash", "AfterUSBPower", "Other"};
  LOG_INF("SLP", "Wake reason %s", kWakeReasonNames[static_cast<int>(wakeupReason)]);
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying the power button is still held");
      if (!gpio.verifyPowerButtonWakeup()) {
        LOG_INF("SLP", "Wake press not held through debounce, back to sleep");
        debug_trace::note("wake press not held, back to sleep");
        logFlush();
        Storage.prepareForDeepSleep();
        powerManager.startDeepSleep(gpio);
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_INF("SLP", "USB power cold boot, back to sleep");
      logFlush();
      Storage.prepareForDeepSleep();
      powerManager.startDeepSleep(gpio);
      break;
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  WakeTiming::noteCost(WakeTiming::Cost::Classify, millis() - classifyStartedMs);

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Old retained-frame state is ignored; every ordinary wake clears its sleep face.
  const BootResume resume = isSilentReboot ? BootResume::Silent : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;

  const bool sleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const std::string pendingWakeBookPath = sleepWake ? APP_STATE.pendingWakeBookPath : std::string();

  const bool paintedFaceWake = resume == BootResume::Splash && wakeupReason == HalGPIO::WakeupReason::PowerButton;
  // Started here because the clear below is armed before routing is known; the routing
  // fields are filled in once they are.
  wake_sequence::WakeInputs wakeInputs;
  wakeInputs.paintedFaceWake = paintedFaceWake;
  wakeInputs.fastUnlock = SETTINGS.fastUnlock != 0;

  setupDisplay(resume != BootResume::Splash || paintedFaceWake, wakeupReason);
  setupBuiltinFonts();

  // How the page gets over the painted sleep face; see wake_sequence::clearStrategy.
  //
  // DriveAll: no pass runs here at all. The reader's own first FAST is asked to drive
  // every pixel, so the wallpaper (or cover, or whatever the lock painted) is driven out
  // by the same ~505 ms waveform that draws the page. One submission instead of a
  // clearing pass plus a paint: 1809 ms less on an X4. The X3 driver promotes that first
  // paint to its ~710 ms half scrub on its own.
  //
  // Blank: a FULL request over a blanked framebuffer, started NOW so the panel drives it
  // while the settle window, the SD font load and the reader's book load all run
  // underneath it. Nothing draws until it completes: BusyBanner, the framebuffer loan
  // and ReaderActivity each wait before their first pixel, and every non-reader route
  // below waits before it paints.
  if (wake_sequence::armsDriveAll(wakeInputs)) display.driveAllPixelsNextFast();
  if (wake_sequence::armsAsyncBlank(wakeInputs)) {
    renderer.clearScreen();
    renderer.displayBufferAsync(HalDisplay::FULL_REFRESH);
  }
  // SD card families after the blank is in flight, so a family read off the card
  // (seconds for a CJK one) costs that wake nothing.
  sdFontSystem.begin(renderer);
  LOG_DBG("MAIN", "Fonts setup");
  WakeTiming::mark(WakeTiming::Stage::DisplayReady);

  // Recovery firmware mode: hold a side button together with the power button at boot to skip
  // directly to the SD-card firmware update screen. This is the way back on a device whose USB
  // flashing the vendor locked, so it must stay reachable on every boot path -- including the
  // one where the card failed to mount, which is why it may already have been answered above
  // (sdRecoveryChord).
  //
  // This runs AFTER the panel is up, not before. The check waits out a fixed window
  // measured from gpio.begin(), and everything that happens inside that window is free:
  // the card mount and the settings load already ran there, and the display bring-up now
  // does too. Before the move it ran after the window and cost its 138 ms (X3) or 50 ms
  // (X4) on top; now the window absorbs it and what is left to wait for shrinks by the
  // same amount.
  const uint32_t settleStartedMs = millis();
  bool recoveryFirmwareMode = sdRecoveryChord;
  if (!recoveryFirmwareMode && wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    recoveryFirmwareMode = recoveryChordHeld(inputStartedMs);
  }
  if (recoveryFirmwareMode)
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)", BoardConfig::isX4Pro() ? "DOWN" : "UP");

  WakeTiming::noteCost(WakeTiming::Cost::Settle, millis() - settleStartedMs);
  WakeTiming::mark(WakeTiming::Stage::InputSettled);

  // Picked here rather than before the display bring-up because it reads
  // recoveryFirmwareMode, which is only known once the check above has run. Still ahead
  // of routing into the chosen book below.
  std::string bootBookPath;
  if (!pendingWakeBookPath.empty()) {
    bootBookPath = pendingWakeBookPath;
  } else if (SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF && !recoveryFirmwareMode &&
             !rebootedFromPanic && resume != BootResume::Silent && APP_STATE.readerActivityLoadCount == 0 &&
             !mappedInputManager.isPressed(MappedInputManager::Button::Back)) {
    bootBookPath = pickBootBookPath();
  }

  const bool oneShotWakeFlagsSet = !APP_STATE.pendingWakeBookPath.empty();
  APP_STATE.pendingWakeBookPath.clear();

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::Splash:
      // Painted-face wakes already armed either the asynchronous blank or DriveAll
      // above. The destination waits for the blank before painting, or supplies the
      // first drive-all frame itself. No extra splash submission is needed.
      if (paintedFaceWake) {
        allowFastInitialReaderRefresh = true;
        break;
      }
      // Not a deep-sleep wake: a flash, a USB boot, a plain restart. goToBoot() runs
      // BootActivity::onEnter inline (no current activity yet), and that paint is
      // blocking, so the splash is already on the panel when this returns.
      activityManager.goToBoot();
      break;
  }

  WakeTiming::mark(WakeTiming::Stage::WakeFaceReady);

  // Wallpaper index reconcile. An X4 unlock arrives as ESP_RST_POWERON (see
  // lib/hal/WakeClassify.h), so reset reason alone cannot separate "wake" from
  // "the card was out". The walk therefore runs only when something says the folder changed: a
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
    const bool wantsWallpaperIndex = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM;
    if (rst != ESP_RST_DEEPSLEEP && !recoveryFirmwareMode && !rebootedFromPanic && wantsWallpaperIndex) {
      if (APP_STATE.sleepIndexDirty || APP_STATE.sleepIndexNeedsRebuild ||
          crosspoint::sleep::windex::indexedFolderChanged() ||
          (rst != ESP_RST_SW && crosspoint::sleep::windex::folderLooksChanged())) {
        renderer.waitRefreshComplete();  // it paints a banner; the blank may still be running
        crosspoint::sleep::windex::reconcileAtColdBoot(renderer);
      }
    }
  }

  // The whole wake decision, once, in one Module. What used to happen here was three
  // passes over the same raw inputs: wake_route::resolve was called, immediately
  // overridden by an inline recovery/panic/silent ternary, and then re-decided by a
  // six-arm if/else chain that read isPressed(Back) and readerActivityLoadCount for the
  // third and fourth time. The tested helper could be — and was — contradicted by the
  // untested layers wrapped around it. wake_sequence::plan() folds all three together, so
  // the overrides are arms of the same decision rather than a correction applied to it.
  const std::string forcedBookPath = pendingWakeBookPath.empty() ? APP_STATE.openEpubPath : pendingWakeBookPath;
  wakeInputs.recoveryFirmwareMode = recoveryFirmwareMode;
  wakeInputs.panic = rebootedFromPanic;
  wakeInputs.silentReboot = resume == BootResume::Silent;
  wakeInputs.silentTargetIsReader = snapshotTarget == SILENT_REBOOT_TARGET_READER;
  wakeInputs.forceBookOnWake = !pendingWakeBookPath.empty();
  wakeInputs.hasForcedBook = !forcedBookPath.empty();
  wakeInputs.openEpubPathEmpty = APP_STATE.openEpubPath.empty();
  wakeInputs.lastSleepFromReader = APP_STATE.lastSleepFromReader;
  wakeInputs.backHeld = mappedInputManager.isPressed(MappedInputManager::Button::Back);
  wakeInputs.bookOnBoot = SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF;
  wakeInputs.readerCrashed = APP_STATE.readerActivityLoadCount > 0;
  wakeInputs.bootBookPicked = !bootBookPath.empty();
  const wake_sequence::WakePlan wakePlan = wake_sequence::plan(wakeInputs);

  // Whether the reader's first page turn has to clean up after a drive-all first paint.
  const bool firstTurnCleans =
      wake_face::firstPageTurnCleans(wake_sequence::clearStrategy(wakeInputs), gpio.deviceIsX3());

  if (oneShotWakeFlagsSet) APP_STATE.saveToFile();

  // Straight-line executor: every decision above it, every side effect below it. The
  // wait rule that used to live only in a comment — "Every route but the reader paints
  // straight from its onEnter, so it waits out a blank still in flight first (no-op
  // otherwise); the reader routes wait inside ReaderActivity::onEnter, after their font
  // and book loads" — is now wakePlan.waitBeforeRoutePaint, asserted per target in
  // test/wake_sequence. It was five scattered call sites and two deliberate omissions; a
  // new arm that forgot the call was a silent double-paint with nothing to catch it.
  if (wakePlan.waitBeforeRoutePaint) renderer.waitRefreshComplete();

  // The crash-loop guard, for every arm that enters the reader. The counter goes up and
  // is COMMITTED before goToReader, which is the whole point: a crash while opening then
  // lands on home next boot instead of trying again forever.
  //
  // Takes the path BY VALUE, deliberately. Two arms pass APP_STATE.openEpubPath and then
  // clear that very field before opening it; a reference would be dangling by the time
  // goToReader read it. The original code copied it into a local for the same reason.
  const auto enterReader = [&](const std::string path, const bool withWakeFlags) {
    if (wakePlan.clearOpenEpubPath) APP_STATE.openEpubPath = "";
    if (wakePlan.bumpReaderLoadCount) {
      APP_STATE.readerActivityLoadCount++;
      APP_STATE.saveToFile();
    }
    if (withWakeFlags) {
      activityManager.goToReader(path, allowFastInitialReaderRefresh, firstTurnCleans);
    } else {
      activityManager.goToReader(path);
    }
  };

  switch (wakePlan.target) {
    case wake_sequence::Target::RecoveryFirmware:
      // Skip normal home/reader routing: jump straight into the SD firmware picker.
      activityManager.replaceActivity(
          std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
      break;
    case wake_sequence::Target::CrashReport:
      // If we rebooted from a panic, go to crash report screen to show the panic info
      activityManager.goToCrashReport();
      break;
    case wake_sequence::Target::SilentReader:
      enterReader(APP_STATE.openEpubPath, /*withWakeFlags=*/false);
      break;
    case wake_sequence::Target::SilentHome:
      // target == home (or reader with no open book): land on home — don't fall
      // through to the sleep-wake "resume reader" logic, which fires on stale
      // openEpubPath + lastSleepFromReader from a prior session.
      activityManager.goHome();
      break;
    case wake_sequence::Target::ForcedReader:
      enterReader(forcedBookPath, /*withWakeFlags=*/true);
      break;
    case wake_sequence::Target::ForcedHome:
      activityManager.goHome(HomeMenuItem::NONE);
      break;
    case wake_sequence::Target::BootBookReader:
      // "Open Book on Boot" jumps straight into a book instead of home: the last-read
      // one, or one of the books in progress at random. Skipped when Back is held (the
      // user is asking for home) or after a reader crash, so a book that cannot open can
      // never wedge boot — that exclusion is inside plan(), which is why this arm can
      // simply open the book.
      enterReader(bootBookPath, /*withWakeFlags=*/false);
      break;
    case wake_sequence::Target::Home:
      // Boot to home screen if no book is open, last sleep was not from reader, back
      // button is held, or reader activity crashed (readerActivityLoadCount > 0).
      activityManager.goHome(HomeMenuItem::NONE);
      break;
    case wake_sequence::Target::ResumeReader:
      // Clear app state to avoid getting into a boot loop if the epub doesn't load
      enterReader(APP_STATE.openEpubPath, /*withWakeFlags=*/true);
      break;
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

// The main loop's idle wait, and the whole reason the IDF power manager has
// anything to work with.
//
// FreeRTOS only reaches tickless light sleep when no task needs to run for
// CONFIG_FREERTOS_IDLE_TIME_BEFORE_SLEEP ticks. delayWallClock() spins on
// vTaskDelay(1), so it never leaves a window longer than a single tick and the
// idle task never once qualifies — the chip would stay awake for every
// millisecond of a reading session with PM enabled and look, from the outside,
// exactly like PM doing nothing. One blocking wait is what opens the window.
//
// The spin is still right in one case: while a render holds the performance
// lock, the render task's BUSY-wait slice light-sleeps the chip by hand and
// stops the FreeRTOS tick while it does (millis() is RTC-corrected on wake, the
// tick is not). A single vTaskDelay through that would overshoot by the whole
// frozen window and starve button sampling mid-refresh, which is the bug
// delayWallClock was written to fix. Nothing is lost by spinning there: a held
// perf lock already keeps the power manager out of light sleep.
static void idlePoll(const unsigned long ms) {
  if (powerManager.isPerfLockHeld()) {
    delayWallClock(ms);
    return;
  }
  vTaskDelay(pdMS_TO_TICKS(ms));
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
  // The mapped manager, not the raw gpio: update() polls the keys AND ticks the release
  // gate (see MappedInputManager::update). The gate is armed by every screen change made
  // while a key is held (ActivityManager's suppressHeldButtonRelease), and only tick()
  // opens it again — polling the raw HAL here left it armed forever, so every
  // wasReleased() in the firmware went dead and Back could never leave a book.
  mappedInputManager.update();

  // Cleared here, once, so that every early return below (the screenshot combo, the sleep
  // paths) leaves the power release ungated. The double-click block further down is the
  // only thing that ever sets it, and only for the pass that sets it.
  mappedInputManager.setPowerReleaseOverride(false, false);
  // Same reason, for the per-button bindings: an early return must never leave a key gated.
  mappedInputManager.clearBindingOverrides();

  renderer.setFadingFix(SETTINGS.fadingFix);
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
    lastActivityTime = millis();  // Reset inactivity timer
  }
  // Publishes the USB console state and takes or releases the WiFi and
  // recent-activity PM locks. The clock itself needs no poke here any more: DFS
  // raises it whenever a lock is taken, and the recent-activity lock is what used
  // to be the explicit setPowerSaving(false) on input.
  powerManager.updateLocks(gpio, millis() - lastActivityTime);
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

  // A hold that woke the device must be released before it can count as a new in-app long
  // press. Otherwise a user who keeps holding after wake would put the device straight
  // back to sleep. This is the whole guard: a two-second window after boot used to sit
  // beside it, and all it did was make the first re-lock after an unlock a dead press.
  static bool powerReleasedSinceWake = false;
  if (!gpio.isPressed(HalGPIO::BTN_POWER)) powerReleasedSinceWake = true;

  // Only while the power hold is still Sleep. Bind that gesture to anything else and this
  // check must stand down, or the device would sleep at sleepHoldMs while the bound action
  // was still waiting for the same hold.
  const uint8_t* powerHoldBinding = SETTINGS.buttonBinding(
      activityManager.isBookContext(), CrossPointSettings::BOUND_BTN_POWER, CrossPointSettings::BOUND_HOLD);
  const bool powerHoldSleeps = powerHoldBinding == nullptr || *powerHoldBinding == CrossPointSettings::LP_MENU_SLEEP;
  if (powerHoldSleeps && powerReleasedSinceWake && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getSleepHoldMs()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Per-button bindings (Settings > Controls > Buttons).
  //
  // Sits below the screenshot combo and both sleep checks, so Power+Down and the sleep
  // hold still win outright, and above every consumer of a button edge, because a press has
  // to be held back before anything acts on it: nothing can tell a single click from the
  // first half of a double until the window closes.
  //
  // A key the router does not intercept is never touched — no gating, no detector, no
  // delay — so paging and hold-to-repeat are exactly what they were for anyone who has not
  // opened the Buttons screen.
  {
    static button_router::Router bindingRouter;
    // The bindings the router is currently armed with. Compared rather than watched: the
    // Buttons screen writes the settings directly, and re-arming every pass would reset
    // the detectors and drop whatever gesture was in flight.
    static uint8_t appliedBindings[button_router::KEY_COUNT][CrossPointSettings::BOUND_GESTURE_COUNT] = {};
    static bool bindingsApplied = false;

    const bool inBook = activityManager.isBookContext();
    uint8_t wanted[button_router::KEY_COUNT][CrossPointSettings::BOUND_GESTURE_COUNT] = {};
    for (uint8_t key = 0; key < button_router::KEY_COUNT; ++key) {
      for (uint8_t gesture = 0; gesture < CrossPointSettings::BOUND_GESTURE_COUNT; ++gesture) {
        const uint8_t* binding = SETTINGS.buttonBinding(inBook, key, gesture);
        wanted[key][gesture] = binding != nullptr ? *binding : CrossPointSettings::LP_MENU_DISABLED;
      }
    }
    if (!bindingsApplied || memcmp(wanted, appliedBindings, sizeof(wanted)) != 0) {
      for (uint8_t key = 0; key < button_router::KEY_COUNT; ++key) {
        // The Home key goes home; the two side keys page. A key still set to what it
        // already did is left alone entirely (Router::intercepts).
        const auto& native = key == CrossPointSettings::BOUND_BTN_HOME    ? button_router::NATIVE_HOME_KEY
                             : key == CrossPointSettings::BOUND_BTN_POWER ? button_router::NATIVE_POWER_KEY
                                                                          : button_router::NATIVE_SIDE_KEY;
        bindingRouter.configure(key,
                                button_router::Binding{wanted[key][CrossPointSettings::BOUND_SINGLE],
                                                       wanted[key][CrossPointSettings::BOUND_DOUBLE],
                                                       wanted[key][CrossPointSettings::BOUND_HOLD]},
                                native);
      }
      // Power holds at the user's own sleepHoldMs, not the shared 500 ms: that is the
      // threshold its hold has always used, and leaving it on Sleep must not change it.
      bindingRouter.setHoldMs(CrossPointSettings::BOUND_BTN_POWER, SETTINGS.getSleepHoldMs());
      memcpy(appliedBindings, wanted, sizeof(wanted));
      bindingsApplied = true;
    }

    // Two actions ActivityManager cannot run: only this file can put the device down, and
    // only this file owns the renderer a forced refresh paints through.
    const auto runBoundFunction = [&](const uint8_t function) {
      if (function == CrossPointSettings::LP_MENU_SLEEP) {
        enterDeepSleep();
        return;
      }
      if (function == CrossPointSettings::LP_MENU_FORCE_REFRESH) {
        LOG_DBG("MAIN", "Manual screen refresh triggered");
        if (!activityManager.handleForcedRefresh()) {
          RenderLock lock;
          renderer.displayBuffer(HalDisplay::HALF_REFRESH);
        }
        return;
      }
      activityManager.runBoundAction(function);
    };

    const uint32_t now = millis();
    // One key's pass through the router: its RAW edges (the overrides below are what
    // rewrite the mapped ones, and feeding the router its own output would latch it),
    // then the hold timer, then the bound action unless the ruling is to replay the
    // gesture the key already does.
    const auto routeKey = [&](const uint8_t key, const bool pressed, const bool released) {
      button_router::Fired fired;
      if (pressed) {
        fired = bindingRouter.onPress(key, now);
      } else if (released) {
        fired = bindingRouter.onRelease(key, now);
      }
      if (!fired.valid) fired = bindingRouter.tick(key, now);
      if (fired.valid)
        debug_trace::note("key %u fired action %u replay=%d", key, fired.function, fired.replayRawEdge ? 1 : 0);
      if (fired.valid && !fired.replayRawEdge) runBoundFunction(fired.function);
      return fired;
    };

    // Left is the upper side key, Right the lower one, matching the hint labels.
    constexpr uint8_t SIDE_HARDWARE[] = {HalGPIO::BTN_UP, HalGPIO::BTN_DOWN};
    // A replayed side-key gesture is two passes long; button_replay::SideKey owns
    // that rule and why each half is needed.
    static button_replay::SideKey replay[2];
    for (uint8_t key = 0; key < 2; ++key) {
      const bool intercepted = bindingRouter.intercepts(key);
      if (!intercepted) {
        replay[key].onPass(/*intercepted=*/false, /*replayNow=*/false);
        continue;
      }
      const uint8_t hardware = SIDE_HARDWARE[key];
      const button_router::Fired fired = routeKey(key, gpio.wasPressed(hardware), gpio.wasReleased(hardware));
      // Edges stay hidden for as long as this key is intercepted, and are replayed only on
      // the pass the router rules the gesture the paging the key already did.
      const button_replay::Injection injection =
          replay[key].onPass(/*intercepted=*/true, fired.valid && fired.replayRawEdge);
      mappedInputManager.setSideKeyOverride(hardware, /*suppressEdges=*/true, bindingRouter.suppressesHold(key),
                                            injection.press, injection.release);
    }

    // Power. Raw edges like a side key, but its release is gated through the same override
    // the wake-hold path already uses, so nothing downstream needs to know the router exists.
    // A hold left on Sleep is not intercepted at all: the sleep-hold check further up keeps
    // it, threshold included.
    if (bindingRouter.intercepts(CrossPointSettings::BOUND_BTN_POWER)) {
      const button_router::Fired fired =
          routeKey(CrossPointSettings::BOUND_BTN_POWER, gpio.wasPressed(HalGPIO::BTN_POWER),
                   gpio.wasReleased(HalGPIO::BTN_POWER));
      mappedInputManager.setPowerReleaseOverride(/*suppress=*/true,
                                                 /*inject=*/fired.valid && fired.replayRawEdge);
    }

    // The Home key reports taps and long presses, never raw edges, so its press and release
    // are manufactured from the tap it already completed. A long press is the hold outright;
    // the detector is reset after one so a tap reported alongside it cannot fire as well.
    constexpr uint8_t HOME_KEY = CrossPointSettings::BOUND_BTN_HOME;
    if (gpio.hasHomeKey() && bindingRouter.intercepts(HOME_KEY)) {
      button_router::Fired fired;
      if (gpio.wasHomeKeyLongPressed()) {
        fired = bindingRouter.fireHold(HOME_KEY);
      } else if (gpio.wasHomeKeyTapped()) {
        bindingRouter.onPress(HOME_KEY, now);
        fired = bindingRouter.onRelease(HOME_KEY, now);
      }
      if (!fired.valid) fired = bindingRouter.tick(HOME_KEY, now);
      // Home is what this key already does, so that binding is answered by replaying the
      // gesture: an activity that must save or confirm first still gets its say.
      if (fired.valid && !fired.replayRawEdge) runBoundFunction(fired.function);
      mappedInputManager.setHomeKeyOverride(/*suppress=*/true, fired.valid && fired.replayRawEdge);
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
    // No delay and no sleep: an activity with a web server up wants the loop back
    // immediately. The clock looks after itself — a running web server means WiFi
    // is up, and updateLocks() holds the WiFi lock at full speed for that.
    yield();  // Give FreeRTOS a chance to run tasks, but return immediately
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

    // One 100 Hz cadence at every idle depth now (see IDLE_POLL_MS for why the old
    // 50 ms stage could not survive the switch to tickless idle). What changed is
    // what happens during the wait: the loop task blocks for the whole period in
    // one go, so the FreeRTOS idle task can light-sleep the chip for almost all of
    // it. The 0 to 1000 ms window used to busy-poll at full attention, and that is
    // the gap this closes.
    if (gpio.isDebouncePending()) {
      // A raw button-state change is mid-debounce: committing needs a second
      // matching sample, so stay awake and poll again quickly rather than hand
      // the window to light sleep. Costs one poll period of awake time per
      // button press, and is the difference between a tap committing and a tap
      // being dropped.
      delayWallClock(HalPowerManager::IDLE_POLL_MS);
    } else {
      idlePoll(HalPowerManager::IDLE_POLL_MS);
    }
  }
}
