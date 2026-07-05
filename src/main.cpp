#include <Arduino.h>
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
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <builtinFonts/all.h>
#include <esp_random.h>

#include <cstring>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

// getSettingsList() builds its ~60-entry SettingInfo list from a single braced
// initializer-list; that backing array (all entries at once, each ~130 B with four
// std::functions + vectors) lands on the loop task's stack during setup()'s settings
// load. The Arduino default of 8 KB left almost no margin, and the status-bar v2
// settings tipped it into a boot-time stack overflow. Give the loop task room.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;

// Fonts
// Default reader font (always compiled in): Bookerly 14. All other
// families/sizes live under OMIT_FONTS so slim builds can drop them.
// The three sans/serif families with no dedicated bold-italic face
// (bookerly/georgia/verdana) reuse their bold face in the bold-italic slot;
// merriweather ships a real bold-italic.
EpdFont bookerly14RegularFont(&bookerly_14_regular);
EpdFont bookerly14BoldFont(&bookerly_14_bold);
EpdFont bookerly14ItalicFont(&bookerly_14_italic);
EpdFontFamily bookerly14FontFamily(&bookerly14RegularFont, &bookerly14BoldFont, &bookerly14ItalicFont,
                                   &bookerly14BoldFont);
#ifndef OMIT_FONTS
EpdFont bookerly11RegularFont(&bookerly_11_regular);
EpdFont bookerly11BoldFont(&bookerly_11_bold);
EpdFont bookerly11ItalicFont(&bookerly_11_italic);
EpdFontFamily bookerly11FontFamily(&bookerly11RegularFont, &bookerly11BoldFont, &bookerly11ItalicFont,
                                   &bookerly11BoldFont);
EpdFont bookerly12RegularFont(&bookerly_12_regular);
EpdFont bookerly12BoldFont(&bookerly_12_bold);
EpdFont bookerly12ItalicFont(&bookerly_12_italic);
EpdFontFamily bookerly12FontFamily(&bookerly12RegularFont, &bookerly12BoldFont, &bookerly12ItalicFont,
                                   &bookerly12BoldFont);
EpdFont bookerly13RegularFont(&bookerly_13_regular);
EpdFont bookerly13BoldFont(&bookerly_13_bold);
EpdFont bookerly13ItalicFont(&bookerly_13_italic);
EpdFontFamily bookerly13FontFamily(&bookerly13RegularFont, &bookerly13BoldFont, &bookerly13ItalicFont,
                                   &bookerly13BoldFont);
EpdFont bookerly15RegularFont(&bookerly_15_regular);
EpdFont bookerly15BoldFont(&bookerly_15_bold);
EpdFont bookerly15ItalicFont(&bookerly_15_italic);
EpdFontFamily bookerly15FontFamily(&bookerly15RegularFont, &bookerly15BoldFont, &bookerly15ItalicFont,
                                   &bookerly15BoldFont);
EpdFont bookerly16RegularFont(&bookerly_16_regular);
EpdFont bookerly16BoldFont(&bookerly_16_bold);
EpdFont bookerly16ItalicFont(&bookerly_16_italic);
EpdFontFamily bookerly16FontFamily(&bookerly16RegularFont, &bookerly16BoldFont, &bookerly16ItalicFont,
                                   &bookerly16BoldFont);

EpdFont georgia11RegularFont(&georgia_11_regular);
EpdFont georgia11BoldFont(&georgia_11_bold);
EpdFont georgia11ItalicFont(&georgia_11_italic);
EpdFontFamily georgia11FontFamily(&georgia11RegularFont, &georgia11BoldFont, &georgia11ItalicFont, &georgia11BoldFont);
EpdFont georgia12RegularFont(&georgia_12_regular);
EpdFont georgia12BoldFont(&georgia_12_bold);
EpdFont georgia12ItalicFont(&georgia_12_italic);
EpdFontFamily georgia12FontFamily(&georgia12RegularFont, &georgia12BoldFont, &georgia12ItalicFont, &georgia12BoldFont);
EpdFont georgia13RegularFont(&georgia_13_regular);
EpdFont georgia13BoldFont(&georgia_13_bold);
EpdFont georgia13ItalicFont(&georgia_13_italic);
EpdFontFamily georgia13FontFamily(&georgia13RegularFont, &georgia13BoldFont, &georgia13ItalicFont, &georgia13BoldFont);
EpdFont georgia14RegularFont(&georgia_14_regular);
EpdFont georgia14BoldFont(&georgia_14_bold);
EpdFont georgia14ItalicFont(&georgia_14_italic);
EpdFontFamily georgia14FontFamily(&georgia14RegularFont, &georgia14BoldFont, &georgia14ItalicFont, &georgia14BoldFont);
EpdFont georgia15RegularFont(&georgia_15_regular);
EpdFont georgia15BoldFont(&georgia_15_bold);
EpdFont georgia15ItalicFont(&georgia_15_italic);
EpdFontFamily georgia15FontFamily(&georgia15RegularFont, &georgia15BoldFont, &georgia15ItalicFont, &georgia15BoldFont);
EpdFont georgia16RegularFont(&georgia_16_regular);
EpdFont georgia16BoldFont(&georgia_16_bold);
EpdFont georgia16ItalicFont(&georgia_16_italic);
EpdFontFamily georgia16FontFamily(&georgia16RegularFont, &georgia16BoldFont, &georgia16ItalicFont, &georgia16BoldFont);

EpdFont verdana11RegularFont(&verdana_11_regular);
EpdFont verdana11BoldFont(&verdana_11_bold);
EpdFont verdana11ItalicFont(&verdana_11_italic);
EpdFontFamily verdana11FontFamily(&verdana11RegularFont, &verdana11BoldFont, &verdana11ItalicFont, &verdana11BoldFont);
EpdFont verdana12RegularFont(&verdana_12_regular);
EpdFont verdana12BoldFont(&verdana_12_bold);
EpdFont verdana12ItalicFont(&verdana_12_italic);
EpdFontFamily verdana12FontFamily(&verdana12RegularFont, &verdana12BoldFont, &verdana12ItalicFont, &verdana12BoldFont);
EpdFont verdana13RegularFont(&verdana_13_regular);
EpdFont verdana13BoldFont(&verdana_13_bold);
EpdFont verdana13ItalicFont(&verdana_13_italic);
EpdFontFamily verdana13FontFamily(&verdana13RegularFont, &verdana13BoldFont, &verdana13ItalicFont, &verdana13BoldFont);
EpdFont verdana14RegularFont(&verdana_14_regular);
EpdFont verdana14BoldFont(&verdana_14_bold);
EpdFont verdana14ItalicFont(&verdana_14_italic);
EpdFontFamily verdana14FontFamily(&verdana14RegularFont, &verdana14BoldFont, &verdana14ItalicFont, &verdana14BoldFont);
EpdFont verdana15RegularFont(&verdana_15_regular);
EpdFont verdana15BoldFont(&verdana_15_bold);
EpdFont verdana15ItalicFont(&verdana_15_italic);
EpdFontFamily verdana15FontFamily(&verdana15RegularFont, &verdana15BoldFont, &verdana15ItalicFont, &verdana15BoldFont);
EpdFont verdana16RegularFont(&verdana_16_regular);
EpdFont verdana16BoldFont(&verdana_16_bold);
EpdFont verdana16ItalicFont(&verdana_16_italic);
EpdFontFamily verdana16FontFamily(&verdana16RegularFont, &verdana16BoldFont, &verdana16ItalicFont, &verdana16BoldFont);

EpdFont merriweather11RegularFont(&merriweather_11_regular);
EpdFont merriweather11BoldFont(&merriweather_11_bold);
EpdFont merriweather11ItalicFont(&merriweather_11_italic);
EpdFont merriweather11BoldItalicFont(&merriweather_11_bolditalic);
EpdFontFamily merriweather11FontFamily(&merriweather11RegularFont, &merriweather11BoldFont, &merriweather11ItalicFont,
                                       &merriweather11BoldItalicFont);
EpdFont merriweather12RegularFont(&merriweather_12_regular);
EpdFont merriweather12BoldFont(&merriweather_12_bold);
EpdFont merriweather12ItalicFont(&merriweather_12_italic);
EpdFont merriweather12BoldItalicFont(&merriweather_12_bolditalic);
EpdFontFamily merriweather12FontFamily(&merriweather12RegularFont, &merriweather12BoldFont, &merriweather12ItalicFont,
                                       &merriweather12BoldItalicFont);
EpdFont merriweather13RegularFont(&merriweather_13_regular);
EpdFont merriweather13BoldFont(&merriweather_13_bold);
EpdFont merriweather13ItalicFont(&merriweather_13_italic);
EpdFont merriweather13BoldItalicFont(&merriweather_13_bolditalic);
EpdFontFamily merriweather13FontFamily(&merriweather13RegularFont, &merriweather13BoldFont, &merriweather13ItalicFont,
                                       &merriweather13BoldItalicFont);
EpdFont merriweather14RegularFont(&merriweather_14_regular);
EpdFont merriweather14BoldFont(&merriweather_14_bold);
EpdFont merriweather14ItalicFont(&merriweather_14_italic);
EpdFont merriweather14BoldItalicFont(&merriweather_14_bolditalic);
EpdFontFamily merriweather14FontFamily(&merriweather14RegularFont, &merriweather14BoldFont, &merriweather14ItalicFont,
                                       &merriweather14BoldItalicFont);
EpdFont merriweather15RegularFont(&merriweather_15_regular);
EpdFont merriweather15BoldFont(&merriweather_15_bold);
EpdFont merriweather15ItalicFont(&merriweather_15_italic);
EpdFont merriweather15BoldItalicFont(&merriweather_15_bolditalic);
EpdFontFamily merriweather15FontFamily(&merriweather15RegularFont, &merriweather15BoldFont, &merriweather15ItalicFont,
                                       &merriweather15BoldItalicFont);
EpdFont merriweather16RegularFont(&merriweather_16_regular);
EpdFont merriweather16BoldFont(&merriweather_16_bold);
EpdFont merriweather16ItalicFont(&merriweather_16_italic);
EpdFont merriweather16BoldItalicFont(&merriweather_16_bolditalic);
EpdFontFamily merriweather16FontFamily(&merriweather16RegularFont, &merriweather16BoldFont, &merriweather16ItalicFont,
                                       &merriweather16BoldItalicFont);

#endif  // OMIT_FONTS

// UI face = Cozette (regular only; no bold cut). The regular Cozette is passed
// into the family's bold slot so a BOLD request (e.g. Lyra card titles) resolves
// to regular Cozette rather than nullptr. UI weight hierarchy comes from size.
EpdFont smallFont(&cozette_10);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&cozette_12);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10RegularFont);

EpdFont ui12RegularFont(&cozette_14);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12RegularFont);

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
  Splash,       // cold boot, flash, panic, or plain reboot
  Silent,       // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  QuickResume,  // wake from a quick-resume deep sleep (SD flag; survives power loss)
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

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
  delay(50);
  ESP.restart();
}

void silentRestartToReader() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = SILENT_REBOOT_TARGET_READER;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

// Verify power button press duration on wake-up from deep sleep
// Pre-condition: isWakeupByPowerButton() == true
void verifyPowerButtonDuration() {
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP) {
    // Fast path for short press
    // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
    return;
  }

  // Give the user up to 1000ms to start holding the power button, and must hold for SETTINGS.getPowerButtonDuration()
  const auto start = millis();
  bool abort = false;
  // Subtract the current time, because inputManager only starts counting the HeldTime from the first update()
  // This way, we remove the time we already took to reach here from the duration,
  // assuming the button was held until now from millis()==0 (i.e. device start time).
  const uint16_t calibration = start;
  const uint16_t calibratedPressDuration =
      (calibration < SETTINGS.getPowerButtonDuration()) ? SETTINGS.getPowerButtonDuration() - calibration : 1;

  gpio.update();
  // Needed because inputManager.isPressed() may take up to ~500ms to return the correct state
  while (!gpio.isPressed(HalGPIO::BTN_POWER) && millis() - start < 1000) {
    delay(10);  // only wait 10ms each iteration to not delay too much in case of short configured duration.
    gpio.update();
  }

  t2 = millis();
  if (gpio.isPressed(HalGPIO::BTN_POWER)) {
    do {
      delay(10);
      gpio.update();
    } while (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.getPowerButtonHeldTime() < calibratedPressDuration);
    abort = gpio.getPowerButtonHeldTime() < calibratedPressDuration;
  } else {
    abort = true;
  }

  if (abort) {
    // Button released too early. Returning to sleep.
    // IMPORTANT: Re-arm the wakeup trigger before sleeping again
    powerManager.startDeepSleep(gpio);
  }
}
void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
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
  APP_STATE.showBootScreen = !isQuickResumeSleep;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
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
  renderer.insertFont(BOOKERLY_14_FONT_ID, bookerly14FontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(BOOKERLY_11_FONT_ID, bookerly11FontFamily);
  renderer.insertFont(BOOKERLY_12_FONT_ID, bookerly12FontFamily);
  renderer.insertFont(BOOKERLY_13_FONT_ID, bookerly13FontFamily);
  renderer.insertFont(BOOKERLY_15_FONT_ID, bookerly15FontFamily);
  renderer.insertFont(BOOKERLY_16_FONT_ID, bookerly16FontFamily);

  renderer.insertFont(GEORGIA_11_FONT_ID, georgia11FontFamily);
  renderer.insertFont(GEORGIA_12_FONT_ID, georgia12FontFamily);
  renderer.insertFont(GEORGIA_13_FONT_ID, georgia13FontFamily);
  renderer.insertFont(GEORGIA_14_FONT_ID, georgia14FontFamily);
  renderer.insertFont(GEORGIA_15_FONT_ID, georgia15FontFamily);
  renderer.insertFont(GEORGIA_16_FONT_ID, georgia16FontFamily);

  renderer.insertFont(VERDANA_11_FONT_ID, verdana11FontFamily);
  renderer.insertFont(VERDANA_12_FONT_ID, verdana12FontFamily);
  renderer.insertFont(VERDANA_13_FONT_ID, verdana13FontFamily);
  renderer.insertFont(VERDANA_14_FONT_ID, verdana14FontFamily);
  renderer.insertFont(VERDANA_15_FONT_ID, verdana15FontFamily);
  renderer.insertFont(VERDANA_16_FONT_ID, verdana16FontFamily);

  renderer.insertFont(MERRIWEATHER_11_FONT_ID, merriweather11FontFamily);
  renderer.insertFont(MERRIWEATHER_12_FONT_ID, merriweather12FontFamily);
  renderer.insertFont(MERRIWEATHER_13_FONT_ID, merriweather13FontFamily);
  renderer.insertFont(MERRIWEATHER_14_FONT_ID, merriweather14FontFamily);
  renderer.insertFont(MERRIWEATHER_15_FONT_ID, merriweather15FontFamily);
  renderer.insertFont(MERRIWEATHER_16_FONT_ID, merriweather16FontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);

  // Discover and load SD card fonts
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
}

// Pick a random non-missing book from the Recent Books list. Returns "" when the
// list is empty or every entry's file is missing from the SD card.
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
  t1 = millis();

#ifdef ENABLE_SERIAL_LOG
  // Earliest possible Serial setup. The 250 ms stall before begin() lets the
  // USB Serial/JTAG peripheral finish power-on and lets the host complete USB
  // enumeration before we touch the CDC state — otherwise cold boot races
  // and the host has to be physically replugged for logs to flow. Warm reboot
  // worked without the delay because USB was already enumerated.
  delay(250);
  Serial.begin(115200);
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif

  HalSystem::begin();

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const uint32_t snapshotTarget =
      (isSilentReboot && silentRebootTarget <= SILENT_REBOOT_TARGET_READER) ? silentRebootTarget : 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;

  gpio.begin();
  powerManager.begin();
  halTiltSensor.begin();
  halClock.begin();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    return;
  }

  HalSystem::checkPanic();

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const auto wakeupReason = gpio.getWakeupReason();
  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                   SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
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
    // Refresh the cached button state a few times — isPressed() needs ~half a second to settle
    // after boot per the HalGPIO contract. Use a millis-based deadline so we always wait the full
    // settle window even if the loop body takes longer than expected on slow boots.
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    if (gpio.isPressed(HalGPIO::BTN_UP)) {
      recoveryFirmwareMode = true;
      LOG_INF("MAIN", "Recovery firmware mode (UP + POWER held at boot)");
    }
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  const BootResume resume = isSilentReboot              ? BootResume::Silent
                            : !APP_STATE.showBootScreen ? BootResume::QuickResume
                                                        : BootResume::Splash;

  setupDisplayAndFonts(resume != BootResume::Splash);

  switch (resume) {
    case BootResume::Silent:
      // Splash skipped: the routing block below picks the target activity; the
      // panel keeps showing the pre-reboot popup until that first paint lands.
      break;
    case BootResume::QuickResume:
      // One-shot flag: re-arm the splash for the next non-quick-resume boot. Save
      // before any painting so a hang in the blocking paint path can't strand
      // us in a quick-resume-with-no-frame loop on the next boot.
      APP_STATE.showBootScreen = true;
      APP_STATE.saveToFile();
      if (loadSleepFrameBuffer()) {
        // Frame restored: swap the sleep moon for the loading icon.
        const auto pageHeight = renderer.getScreenHeight();
        renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      } else {
        activityManager.goToBoot();  // frame file missing, fall back to the splash
      }
      break;
    case BootResume::Splash:
      activityManager.goToBoot();
      break;
  }

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivity(
        std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInputManager, /*recoveryMode=*/true));
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (resume == BootResume::Silent && snapshotTarget == SILENT_REBOOT_TARGET_READER &&
             !APP_STATE.openEpubPath.empty()) {
    // Crash guard: a render/load fault here silent-reboots straight back into the
    // same book. readerActivityLoadCount is bumped on each such open and cleared
    // only by a clean reader exit (onExit), so once it has climbed the book is
    // wedging boot — fall back to Home instead of looping forever. The >= 2
    // threshold tolerates a single legitimate silent restart (e.g. a fragmentation
    // reboot mid-read) still resuming the book.
    if (APP_STATE.readerActivityLoadCount >= 2) {
      APP_STATE.readerActivityLoadCount = 0;
      APP_STATE.openEpubPath = "";
      APP_STATE.saveToFile();
      activityManager.goHome();
    } else {
      APP_STATE.readerActivityLoadCount++;
      APP_STATE.saveToFile();
      activityManager.goToReader(APP_STATE.openEpubPath);
    }
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0).
    //
    // "Open random book on boot": on a genuine boot-to-home, jump straight into a
    // random ongoing book instead. Skipped when Back is held (the user wants home)
    // or after a reader crash (readerActivityLoadCount > 0), so it can't wedge boot.
    const bool backHeld = mappedInputManager.isPressed(MappedInputManager::Button::Back);
    std::string randomBookPath;
    if (SETTINGS.openRandomRecentOnBoot && !backHeld && APP_STATE.readerActivityLoadCount == 0) {
      randomBookPath = pickRandomRecentBookPath();
    }
    if (!randomBookPath.empty()) {
      // Guard against a bad book bricking boot the same way the resume path does:
      // bump the crash-loop counter so a crash on open lands on home next boot.
      APP_STATE.readerActivityLoadCount++;
      APP_STATE.saveToFile();
      activityManager.goToReader(randomBookPath);
    } else {
      activityManager.goHome();
    }
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    activityManager.goToReader(path);
  }

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

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.update();
  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());

  renderer.setFadingFix(SETTINGS.fadingFix);

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

  // Check for any user activity (button press or release), a button still held
  // down (hold-to-scroll), or active background work. isAnyPressed() keeps the
  // inactivity timer fresh for the whole duration of a hold so the CPU never
  // throttles mid-press and page-turns stay snappy.
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.isAnyPressed() || halTiltSensor.hadActivity() ||
      activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
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

  if (millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    RenderLock lock;
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }

  const unsigned long activityStartTime = millis();
  activityManager.loop();
  const unsigned long activityDuration = millis() - activityStartTime;

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
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive.
      // NOTE: keep this at 10ms. The render task shares priority 1 with this
      // loop, so a shorter delay makes the loop wake often enough to preempt the
      // render task mid e-ink waveform, which corrupts the refresh and ghosts
      // every screen. The isAnyPressed() keepalive above is what actually makes
      // hold-to-scroll snappy; the loop delay must not be dropped for it.
      delay(10);
    }
  }
}
