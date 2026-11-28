#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalScreenCalibration.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include <esp_timer.h>

#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderAutoSync.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReaderPresetStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "SilentRestart.h"
#include "SleepTiming.h"
#include "UiFont.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "activities/boot_sleep/QuickResumeFrame.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "activities/util/LowBatteryNoticeActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "frontlight/FrontlightBootPolicy.h"
#include "network/FirmwareSwitchAudit.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "sleep/WakeFacePolicy.h"
#include "sleep/WakeSequence.h"
#include "util/BookProgressFile.h"
#include "util/ButtonNavigator.h"
#include "util/ButtonReplay.h"
#include "util/ButtonRouter.h"
#include "util/CredentialBundle.h"
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

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,  // cold boot, flash, panic, or plain reboot
  Silent,  // heap-defrag ESP.restart() (RTC flag; lost on power loss)
};

// Defined below setup()'s helpers.
static std::string pickRandomRecentBookPath();
static std::string pickBootBookPath();

// Boot-time "not a real wake" exits: nothing was painted or changed, so none of
// enterDeepSleep()'s state save or sleep face applies.
[[noreturn]] static void sleepAgain() {
  logFlush();
  Storage.prepareForDeepSleep();
  powerManager.setChargeWake(SETTINGS.wakeOnUsbPlugIn != 0);
  powerManager.startDeepSleep();
}

// Kept out of line so its log buffers (768 bytes) are not on the loop stack while
// goToSleep() above it paints the sleep face, the deepest call the lock makes.
[[noreturn]] __attribute__((noinline)) static void finishDeepSleep(const unsigned long sleepT0,
                                                                   const unsigned long sleepTState,
                                                                   const unsigned long sleepTPaint,
                                                                   const unsigned long sleepTFrame,
                                                                   const unsigned long sleepTWifi);

[[noreturn]] void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  // Lock cost, stage by stage: each stamp is taken after the step it names, and the serial
  // report prints the difference between neighbours.
  const unsigned long sleepT0 = millis();
  SleepTiming::begin();
  unsigned long sleepTState = sleepT0;
  unsigned long sleepTPaint = sleepT0;
  unsigned long sleepTFrame = sleepT0;
  unsigned long sleepTWifi = sleepT0;
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  // ponytail: no save here. finishDeepSleep() below writes APP_STATE once after the paint,
  // and a JSON rewrite per lock stage was two redundant SD writes on every lock.
  sleepTState = millis();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  markDeepSleepCommitted();
  activityManager.goToSleep(fromTimeout);
  sleepTPaint = millis();
  // Behind the sleep face: the reader's onExit above has saved the position it pushes.
  ko_auto_sync::onSleep();
  // Quick Resume keeps the page on the panel, so the wake needs it back in the framebuffer.
  if (wake_face::usesQuickResumeForSleep(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen, fromTimeout)) {
    quick_resume::saveFrame(renderer);
  } else {
    quick_resume::clearFrame();
  }

  sleepTFrame = millis();

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  sleepTWifi = millis();
  finishDeepSleep(sleepT0, sleepTState, sleepTPaint, sleepTFrame, sleepTWifi);
}

static void finishDeepSleep(const unsigned long sleepT0, const unsigned long sleepTState,
                            const unsigned long sleepTPaint, const unsigned long sleepTFrame,
                            const unsigned long sleepTWifi) {
  // The lock's only APP_STATE write: everything set above and in SleepActivity lands with it.
  APP_STATE.saveToFile();

  const unsigned long sleepTBudget = millis();
  // The paint stage is the one worth breaking down: it was 10112 ms of a 10258 ms lock
  // when the stages were first measured, and only about 4800 ms of that was the panel.
  char paintStages[320];
  SleepTiming::format(paintStages, sizeof(paintStages));
  char sleepNote[400];
  snprintf(sleepNote, sizeof(sleepNote), "sleep state=%lu paint=%lu frame=%lu wifi=%lu save=%lu [%s]",
           sleepTState - sleepT0, sleepTPaint - sleepTState, sleepTFrame - sleepTPaint, sleepTWifi - sleepTFrame,
           sleepTBudget - sleepTWifi, paintStages);

  // LEDC stops in deep sleep with the pads in whatever state isolation leaves
  // them; drive the light to zero first. Not saved: the next boot's
  // FrontlightBootPolicy still decides from SETTINGS.frontlightOn.
  Frontlight.setOn(false);
  display.deepSleep();
  Storage.prepareForDeepSleep();
  const unsigned long sleepTPanel = millis();
  // INF, not DBG: a release kit runs at LOG_LEVEL 1 and this is the one line that says
  // what a lock cost.
  LOG_INF("SLP", "Lock %lu ms total (%s panel=%lu)", sleepTPanel - sleepT0, sleepNote, sleepTPanel - sleepTBudget);
  LOG_INF("MAIN", "Entering deep sleep");
  // Last chance: startDeepSleep() does not return, and the USB-CDC link dies with the
  // chip, so anything still buffered here is lost.
  logFlush();

  powerManager.setChargeWake(SETTINGS.wakeOnUsbPlugIn != 0);
  powerManager.startDeepSleep();
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
  uint8_t calibratedEdges[4];
  if (HalScreenCalibration::load(calibratedEdges)) {
    const gfx::ViewableInsets calibrated{calibratedEdges[0], calibratedEdges[1], calibratedEdges[2],
                                         calibratedEdges[3]};
    if (gfx::validCalibration(calibrated)) renderer.setViewableInsets(calibrated);
  }
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
  fontDecompressor.init();
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  bindBuiltinFonts(renderer);
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
  BoardConfig::holdPowerRails();

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
  const esp_reset_reason_t rst = esp_reset_reason();
  const bool watchedBoot = rst != ESP_RST_DEEPSLEEP && rst != ESP_RST_POWERON;
  if (watchedBoot) delay(250);
  Serial.begin(115200);
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Dev env only, and only on the boots a developer watches (the ones above): hold until a
  // serial monitor attaches so the first lines are not lost, for at most a second more.
  // Release envs keep the fixed 250 ms above and nothing else.
  if (watchedBoot) {
    for (uint32_t waited = 0; !logSerial && waited < 1000; waited += 10) delay(10);
  }
#endif
  // The loop reads serial commands with readStringUntil; the 1 s default stalls every pass on a partial line.
  logSerial.setTimeout(20);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  const bool rebootedFromPanic = HalSystem::isRebootFromPanic();

  const RestartBoot restartBoot = takeRestartBoot();
  const bool isSilentReboot = restartBoot.silent;

  gpio.begin();
  // When the ADC button ladder came up. The recovery-combo check below needs the ladder to
  // have settled, and "settled" is time since this call, not time since that check is
  // reached — see the deadline there.
  const unsigned long inputStartedMs = millis();
  powerManager.begin();
  halClock.begin();

  LOG_INF("MAIN", "Hardware detect: %s", gpio.deviceIsX3() ? "X3" : "X4");

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  bool sdRecoveryChord = false;
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
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
      activityManager.goToFullScreenMessage(tr(STR_SD_CARD_ERROR), EpdFontFamily::REGULAR);
      return;
    }
    activityManager.goToFullScreenMessage(tr(STR_INSERT_SD_WITH_FIRMWARE), EpdFontFamily::REGULAR);
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
      activityManager.goToFullScreenMessage(tr(STR_SD_CARD_ERROR), EpdFontFamily::REGULAR);
      return;
    }
    LOG_INF("MAIN", "SD card mounted on retry; entering recovery firmware mode");
  }

  HalSystem::checkPanic();

  // If a firmware update handed the device to another image and the bootloader
  // refused it, we are running the old firmware right now and nothing else
  // would say so. Write that to the SD card while it is fresh.
  firmware_flash::auditPendingSwitch(CROSSPOINT_VERSION);

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

  // A credential bundle left by a power cut or a crash mid-share still holds every
  // WiFi and OPDS password in the clear. No screen is open yet, so none is in use.
  for (const char* bundle : {credential_bundle::STAGING_PATH, credential_bundle::LEGACY_PATH}) {
    for (const std::string& path : {std::string(bundle), FsHelpers::partialPathFor(bundle)}) {
      if (Storage.exists(path.c_str()) && Storage.remove(path.c_str())) {
        LOG_INF("MAIN", "Removed stale credential bundle %s", path.c_str());
      }
    }
  }

  SETTINGS.loadFromFile();
  APP_STATE.loadFromFile();
  RECENT_BOOKS.loadFromFile();
  // One-time upgrade: books read before the reading badges existed have a percentage in
  // the recents list and no marker beside their cache. Seeding costs at most thirteen
  // small writes and only ever happens once per card.
  if (!APP_STATE.readingBadgesSeeded) {
    book_progress::backfillFromRecents();
    APP_STATE.readingBadgesSeeded = true;
    APP_STATE.saveToFile();
  }
  OPDS_STORE.loadFromFile();
  // Adds the shipped library entry on a card that has never seen it, so it is there
  // to fill credentials into rather than typed out on a five-button keyboard.
  OPDS_STORE.seedBuiltInServers();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  // Brightness and warmth always come back; whether the light itself does is
  // FrontlightBootPolicy's call. Inert on a board without a frontlight.
  SETTINGS.clampFrontlightBrightness();
  // A clock that does not answer leaves the schedule out rather than guessing.
  bool scheduledOn = false;
  uint16_t nowMinutes = 0;
  uint8_t bootWarmth = SETTINGS.activeFrontlightWarmth();
  if (SETTINGS.frontlightScheduleEnabled != 0 &&
      halClock.localMinutes(nowMinutes, SETTINGS.clockTimezone, SETTINGS.clockDst)) {
    const int onMin = SETTINGS.frontlightScheduleOn * frontlight::SCHEDULE_STEP_MINUTES;
    const int offMin = SETTINGS.frontlightScheduleOff * frontlight::SCHEDULE_STEP_MINUTES;
    scheduledOn = frontlight::insideSchedule(onMin, offMin, nowMinutes);
    if (Frontlight.hasColorTemperature()) {
      bootWarmth = frontlight::scheduledWarmth(onMin, offMin, nowMinutes, bootWarmth);
    }
    LOG_INF("MAIN", "Light schedule: %02d:%02d local, %s", nowMinutes / 60, nowMinutes % 60,
            scheduledOn ? "inside" : "outside");
  }
  Frontlight.begin(SETTINGS.activeFrontlightBrightness(), bootWarmth,
                   frontlight::restoreLightOnAtBoot({SETTINGS.frontlightOn != 0, SETTINGS.frontlightRestoreOnWake != 0,
                                                     isSilentReboot, restartBoot.lightOn, scheduledOn}));

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
        // No press, but the charger line is charging: the Wake on USB Plug-In wake. Boot on as for a
        // button wake (same painted sleep face), minus the held-button bookkeeping.
        const bool plugInWakeOn = SETTINGS.wakeOnUsbPlugIn != 0 && HalPowerManager::supportsChargeWake();
        if (!wake_classify::isPlugInWake(plugInWakeOn, false, plugInWakeOn && HalPowerManager::isCharging())) {
          LOG_INF("SLP", "Wake press not held through debounce, back to sleep");
          sleepAgain();
        }
        LOG_INF("SLP", "USB plug-in wake");
        break;
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // If USB power caused a cold boot, go back to sleep
      LOG_INF("SLP", "USB power cold boot, back to sleep");
      sleepAgain();
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Every wake clears its sleep face, except Quick Resume's, which setup() puts back below.
  const BootResume resume = isSilentReboot ? BootResume::Silent : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;

  const bool paintedFaceWake = resume == BootResume::Splash && wakeupReason == HalGPIO::WakeupReason::PowerButton;
  // Started here because the clear below is armed before routing is known; the routing
  // fields are filled in once they are.
  wake_sequence::WakeInputs wakeInputs;
  wakeInputs.paintedFaceWake = paintedFaceWake;
  wakeInputs.fastUnlock = SETTINGS.fastUnlock != 0;

  setupDisplay(resume != BootResume::Splash || paintedFaceWake, wakeupReason);
  noteRestartDisplayUp();
  setupBuiltinFonts();

  // Quick Resume: the panel still holds the page the lock left. Put it back in the
  // framebuffer and arm no clear at all. The destination's first paint is promoted to a
  // clean pass by both X4 drivers anyway, and on the X3 the restored frame is the baseline
  // of that first paint. A missing or short frame wakes like any other painted face.
  if (paintedFaceWake && wake_face::quickResumePossible(SETTINGS.sleepScreen, SETTINGS.quickResumeSleepScreen) &&
      quick_resume::restoreFrame()) {
    wakeInputs.paintedFaceWake = false;
    if (gpio.deviceIsX3()) renderer.cleanupGrayscaleWithFrameBuffer();
  }

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
  bool recoveryFirmwareMode = sdRecoveryChord;
  if (!recoveryFirmwareMode && wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    recoveryFirmwareMode = recoveryChordHeld(inputStartedMs);
  }
  if (recoveryFirmwareMode)
    LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)", BoardConfig::isX4Pro() ? "DOWN" : "UP");

  // Picked here rather than before the display bring-up because it reads
  // recoveryFirmwareMode, which is only known once the check above has run. Still ahead
  // of routing into the chosen book below.
  std::string bootBookPath;
  if (SETTINGS.bootBookMode != CrossPointSettings::BOOT_BOOK_OFF && !recoveryFirmwareMode && !rebootedFromPanic &&
      resume != BootResume::Silent && APP_STATE.readerActivityLoadCount == 0 &&
      !mappedInputManager.isPressed(MappedInputManager::Button::Back)) {
    bootBookPath = pickBootBookPath();
  }

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
    const bool wantsWallpaperIndex = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM ||
                                     SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM;
    if (!recoveryFirmwareMode && !rebootedFromPanic && wantsWallpaperIndex) {
      // On deep-sleep wakes (X3, X4 Pro), skip the SD directory probe when nothing flagged
      // the index; still honor explicit dirty/rebuild flags so a stale index heals next wake.
      const bool coldBootProbe =
          rst != ESP_RST_DEEPSLEEP && (crosspoint::sleep::windex::indexedFolderChanged() ||
                                       (rst != ESP_RST_SW && crosspoint::sleep::windex::folderLooksChanged()));
      if (APP_STATE.sleepIndexDirty || APP_STATE.sleepIndexNeedsRebuild || coldBootProbe) {
        renderer.waitRefreshComplete();  // it paints a banner; the blank may still be running
        crosspoint::sleep::windex::reconcileAtColdBoot(renderer);
      }
    }
  }

  // The whole wake decision, once, in one Module: wake_sequence::plan() reads each raw
  // input once, so no untested layer can contradict it.
  wakeInputs.recoveryFirmwareMode = recoveryFirmwareMode;
  wakeInputs.panic = rebootedFromPanic;
  wakeInputs.silentReboot = resume == BootResume::Silent;
  wakeInputs.silentTarget = restartBoot.target;
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
  // Takes the path BY VALUE, deliberately. The resume arm passes APP_STATE.openEpubPath and
  // then clears that very field before opening it; a reference would be dangling by the time
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
      // Back out of KOReader Sync: the menu that launched it comes back over the page. Set after
      // goToReader(), which forgets it; the book screen itself enters on the next loop pass.
      if (restartBoot.readerMenu) {
        activityManager.rememberReaderMenu(MenuReturn{restartBoot.readerMenu, nullptr, restartBoot.readerMenuRow});
      }
      break;
    case wake_sequence::Target::SilentHome:
      // target == home (or reader with no open book): land on home — don't fall
      // through to the sleep-wake "resume reader" logic, which fires on stale
      // openEpubPath + lastSleepFromReader from a prior session. A radio screen's reboot
      // lands where its Back would have (silentRestartBack()); a plain one, Home.
      activityManager.followBackTrail(restartBoot.backTrail);
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

  noteRestartLoopPass();

  // Cleared here, once, so that every early return below (the screenshot combo, the sleep
  // paths) leaves the power release ungated. The double-click block further down is the
  // only thing that ever sets it, and only for the pass that sets it.
  mappedInputManager.setPowerReleaseOverride(false, false);
  // Same reason, for the per-button bindings: an early return must never leave a key gated.
  mappedInputManager.clearBindingOverrides();

  // USB Drive has lent the SD card to a computer. Everything below can reach for the
  // card (sleep, screenshots, bindings, screen changes), so only the activity runs.
  if (activityManager.requiresExclusiveStorageLoop()) {
    activityManager.loop();
    // Full clock throughout. The host reads the card through TinyUSB, behind the storage
    // lock that guards the idle clock, and this loop only runs on a USB cable's power.
    powerManager.setPowerSaving(false);
    // With no host active a slower loop is safe. The activity itself times out the
    // raw-storage handoff rather than entering deep sleep detached.
    delay(activityManager.preventAutoSleep() ? 10 : 50);
    return;
  }

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
        // The render task draws into the same buffer, and it is null while lent to a section build.
        RenderLock lock;
        const uint8_t* buf = display.getFrameBuffer();
        if (buf) logSerial.write(buf, bufferSize);
        logSerial.printf(buf ? "SCREENSHOT_END\n" : "SCREENSHOT_ERROR:no framebuffer\n");
      }
    }
  }

  // Check for any user activity (button press or release) or active background work
  static unsigned long lastActivityTime = millis();
  // Touch counts: before Touch Sleep it did not, so a touch-only session never
  // reset the auto-sleep clock or took the post-input activity lock.
  // A locked device in a bag is pressed against all day and must still go to sleep.
  const bool quickLocked = activityManager.isQuickLocked();
  if ((mappedInputManager.isInputActive() && !quickLocked) || activityManager.preventAutoSleep()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }
  // Any input wakes touch (only a button can, while it sleeps); the wake itself
  // finishes over the next few polls.
  const unsigned long touchSleepMs = SETTINGS.touchSleepMs();
  // Not while locked: the timer no longer tracks input, and the Home key may be what unlocks.
  gpio.setTouchAsleep(!quickLocked && touchSleepMs > 0 && millis() - lastActivityTime >= touchSleepMs);

  // Let wake continue as soon as its hold has been verified. The release can
  // arrive after setup, so consume that one input frame rather than making it
  // a page turn, refresh, or other short power-button action.
  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotComboActive = false;
  if (!quickLocked && gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    if (!screenshotComboActive) {
      screenshotComboActive = true;
      RenderLock lock;
      ScreenshotUtil::takeScreenshot(renderer);
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    screenshotComboActive = false;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) return;
  }

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
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
    static uint8_t appliedLayout = 0;

    const bool inBook = activityManager.isBookContext();
    // The side layout only reaches the reader's PageBack/PageForward; lists page by key.
    const uint8_t layout = inBook ? SETTINGS.sideButtonLayout : static_cast<uint8_t>(CrossPointSettings::PREV_NEXT);
    uint8_t wanted[button_router::KEY_COUNT][CrossPointSettings::BOUND_GESTURE_COUNT] = {};
    for (uint8_t key = 0; key < button_router::KEY_COUNT; ++key) {
      for (uint8_t gesture = 0; gesture < CrossPointSettings::BOUND_GESTURE_COUNT; ++gesture) {
        const uint8_t* binding = SETTINGS.buttonBinding(inBook, key, gesture);
        wanted[key][gesture] = binding != nullptr ? *binding : CrossPointSettings::LP_MENU_DISABLED;
      }
      // A side key still on its factory paging (left back, right forward) pages the way the
      // layout says; a binding the user set to anything else is theirs and stays absolute.
      if (key < 2 && wanted[key][CrossPointSettings::BOUND_SINGLE] == button_router::sideKeyPage(key, 0)) {
        wanted[key][CrossPointSettings::BOUND_SINGLE] = button_router::sideKeyPage(key, layout);
      }
    }
    if (!bindingsApplied || layout != appliedLayout || memcmp(wanted, appliedBindings, sizeof(wanted)) != 0) {
      for (uint8_t key = 0; key < button_router::KEY_COUNT; ++key) {
        // A key still set to what it already did is left alone entirely (Router::intercepts).
        bindingRouter.configure(key,
                                button_router::Binding{wanted[key][CrossPointSettings::BOUND_SINGLE],
                                                       wanted[key][CrossPointSettings::BOUND_DOUBLE],
                                                       wanted[key][CrossPointSettings::BOUND_HOLD]},
                                button_router::nativeFor(key, layout));
      }
      appliedLayout = layout;
      // Power holds at the user's own sleepHoldMs, not the shared 500 ms: that is the
      // threshold its hold has always used, and leaving it on Sleep must not change it.
      bindingRouter.setHoldMs(CrossPointSettings::BOUND_BTN_POWER, SETTINGS.getSleepHoldMs());
      memcpy(appliedBindings, wanted, sizeof(wanted));
      bindingsApplied = true;
    }

    bool confirmFired = false;
    // Sleep goes straight down from here: this file owns the device.
    const auto runBoundFunction = [&](const uint8_t function) {
      if (function == CrossPointSettings::LP_MENU_QUICK_LOCK) {
        activityManager.toggleQuickLock();
        return;
      }
      if (activityManager.isQuickLocked()) return;
      if (function == CrossPointSettings::LP_MENU_SLEEP) {
        enterDeepSleep();
      }
      if (function == CrossPointSettings::LP_MENU_CONFIRM) {
        confirmFired = true;
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

    // Two-pass synthetic Confirm for LP_MENU_CONFIRM: press on this pass, release on the next
    // unless an activity transition suppressed it.
    static button_replay::SideKey confirmReplay;
    if (mappedInputManager.takeConfirmReleaseSuppressed()) confirmReplay.reset();
    const button_replay::Injection confirmInjection = confirmReplay.onPass(/*intercepted=*/true, confirmFired);
    mappedInputManager.setConfirmOverride(confirmInjection.press, confirmInjection.release);
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
  // Time blocked in the activity (e.g. a synchronous OPDS download) is not idle time.
  // Only long blocks count: a normal pass takes a few ms and must not slow the timer.
  if (activityDuration >= 1000) lastActivityTime += activityDuration;

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // The hint reads activity state the render task may be replacing (the reader's
  // section), so ask it under the render lock. A busy lock means a render is running:
  // spinning on yield() would only steal its CPU (single-core C3), so step aside instead.
  bool skipLoopDelay = false;
  {
    RenderLock lock(RenderLock::TryOnly{});
    if (!lock.owns()) {
      // Let rendering advance without treating lock contention as idle.
      delay(10);
      return;
    }
    skipLoopDelay = activityManager.skipLoopDelay();
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (skipLoopDelay) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    const unsigned long idleMs = millis() - lastActivityTime;
    // Drop the panel's rails once the device sits.
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

    if (idleMs >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      // Sleep in short slices and wake the poll as soon as a button contact closes.
      // InputManager commits a press only when two consecutive polls agree, so a
      // press shorter than one 50 ms sleep could land in a single sample and be lost.
      const unsigned long idleStart = millis();
      while (millis() - idleStart < 50) {
        delay(10);
        if (gpio.rawInputActive()) break;
      }
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
