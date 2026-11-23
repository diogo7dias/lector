#include "SilentRestart.h"

#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>
#include <esp_attr.h>
#include <esp_timer.h>

#include "CrossPointState.h"
#include "activities/Activity.h"
#include "components/UITheme.h"

namespace {
// RTC_NOINIT survives ESP.restart() but not power loss.
static RTC_NOINIT_ATTR uint32_t silentRebootMagic;
static RTC_NOINIT_ATTR uint32_t silentRebootTarget;
static RTC_NOINIT_ATTR uint32_t silentRebootPayload;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
constexpr uint32_t SILENT_REBOOT_TARGET_HOME = 0;
constexpr uint32_t SILENT_REBOOT_TARGET_READER = 1;
constexpr uint32_t SILENT_REBOOT_TARGET_SETTINGS = 2;
constexpr uint32_t SILENT_REBOOT_LIGHT_ON = 1U << 0;

// Breadcrumbs for the heap-defrag reboot, in RTC memory because the card is one of the
// suspects: an X3 that left a WiFi screen sat on "Loading" until Power was held, and
// nothing said which step it stopped at. The trail names the last step passed; the
// rescue timer reboots a restart that has not reached its first frame in time.
static RTC_NOINIT_ATTR uint32_t restartTrail;
constexpr uint32_t RESTART_TRAIL_MAGIC = 0x7EA11000;
constexpr uint32_t RESTART_TRAIL_MASK = 0xFFFFFF00;
enum RestartStage : uint8_t {
  STAGE_ENTERED = 1,
  STAGE_POPUP_DRAWN,
  STAGE_STATE_SAVED,
  STAGE_RESTART_CALLED,
  STAGE_BOOT_SILENT,
  STAGE_DISPLAY_UP,
  STAGE_LOOP_RUNNING,
};
constexpr uint64_t RESTART_RESCUE_US = 30ULL * 1000 * 1000;
static esp_timer_handle_t restartRescueTimer = nullptr;

static void markRestartStage(const RestartStage stage) { restartTrail = RESTART_TRAIL_MAGIC | stage; }

// Decodes a breadcrumb word; 0 when it is cold-boot garbage or cleared.
static uint8_t restartStageOf(const uint32_t word) {
  if ((word & RESTART_TRAIL_MASK) != RESTART_TRAIL_MAGIC) return 0;
  const uint8_t stage = word & 0xFF;
  return stage <= STAGE_LOOP_RUNNING ? stage : 0;
}

// esp_timer task context: no card, no display, no mutex. Only the RTC word and a reset.
// The magic goes too, so the rescue boot is a plain one: if the seamless path is what
// hangs, a silent rescue would only hang again, every 30 s.
static void restartRescueFired(void*) {
  silentRebootMagic = 0;
  esp_restart();
}

static void armRestartRescue() {
  if (!restartRescueTimer) {
    const esp_timer_create_args_t args = {.callback = restartRescueFired, .name = "rstrescue"};
    if (esp_timer_create(&args, &restartRescueTimer) != ESP_OK) return;
  }
  esp_timer_stop(restartRescueTimer);  // harmless when not running
  esp_timer_start_once(restartRescueTimer, RESTART_RESCUE_US);
}

// The restart landed its first frame: the trail has nothing left to say.
static void finishRestartTrail() {
  if (restartTrail == 0) return;
  if (restartRescueTimer) esp_timer_stop(restartRescueTimer);
  restartTrail = 0;
}

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
bool deepSleepInProgress = false;

}  // namespace

// SETTINGS.frontlightOn is the saved preference, not the live state: a wake with
// Restore Light on Wake off leaves the light dark while "was on" stays saved. The
// live state rides across the reboot so the light comes back exactly as it was.
static void armSilentReboot(const uint32_t target) {
  silentRebootTarget = target;
  silentRebootPayload = Frontlight.isOn() ? SILENT_REBOOT_LIGHT_ON : 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
}

static void silentRestartTo(const uint32_t target) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  armRestartRescue();
  markRestartStage(STAGE_ENTERED);
  armSilentReboot(target);
  LOG_DBG("MAIN", "Silent restart (target=%lu)", static_cast<unsigned long>(target));
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(activityManager.getRenderer(), tr(STR_LOADING_POPUP));
  markRestartStage(STAGE_POPUP_DRAWN);
  APP_STATE.saveToFile();
  markRestartStage(STAGE_STATE_SAVED);
  delay(50);
  markRestartStage(STAGE_RESTART_CALLED);
  ESP.restart();
}

void silentRestart() { silentRestartTo(SILENT_REBOOT_TARGET_HOME); }

void silentRestartToReader() { silentRestartTo(SILENT_REBOOT_TARGET_READER); }

void silentRestartToSettings() { silentRestartTo(SILENT_REBOOT_TARGET_SETTINGS); }

void teardownWifiAndRestart(void (*restart)()) {
  if (WiFi.getMode() == WIFI_MODE_NULL) return;  // WiFi never came up: nothing to defrag
  WiFi.disconnect(false);
  delay(30);
  restart();
}

void markDeepSleepCommitted() { deepSleepInProgress = true; }

RestartBoot takeRestartBoot() {
  RestartBoot boot;
  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too: RTC_NOINIT memory is uninitialized on cold boot.
  boot.silent = silentRebootMagic == SILENT_REBOOT_MAGIC;
  if (boot.silent && silentRebootTarget == SILENT_REBOOT_TARGET_READER) boot.target = RestartTarget::Reader;
  if (boot.silent && silentRebootTarget == SILENT_REBOOT_TARGET_SETTINGS) boot.target = RestartTarget::Settings;
  boot.lightOn = boot.silent && (silentRebootPayload & SILENT_REBOOT_LIGHT_ON) != 0;
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootPayload = 0;
  if (boot.silent) {
    armRestartRescue();
    restartTrail = RESTART_TRAIL_MAGIC | STAGE_BOOT_SILENT;
  } else {
    restartTrail = 0;
  }
  return boot;
}

void noteRestartDisplayUp() {
  if (restartTrail) markRestartStage(STAGE_DISPLAY_UP);
}

void noteRestartLoopPass() {
  if (!restartTrail) return;
  if (restartStageOf(restartTrail) < STAGE_LOOP_RUNNING) markRestartStage(STAGE_LOOP_RUNNING);
  if (display.hasCompletedRefresh()) finishRestartTrail();  // a refresh has completed since boot
}
