#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "MenuReturn.h"
#include "components/LightPanel.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

enum class HomeMenuItem {
  NONE,
  BOOKORBIT_MISSING,
  FILE_BROWSER,
  LIBRARY,
  OPDS_BROWSER,
  FILE_TRANSFER,
  SETTINGS_MENU,
  QUICK_PANEL,
};

// The Home row for the screen being left, so Home opens with that row selected.
HomeMenuItem homeRowFor(const std::string& activityName);

/**
 * ActivityManager
 *
 * This mirrors the same concept of Activity in Android, where an activity represents a single screen of the UI. The
 * manager is responsible for launching activities, and ensuring that only one activity is active at a time.
 *
 * It also provides a stack mechanism to allow activities to launch sub-activities and get back the results when the
 * sub-activity is done. For example, the WebServer activity can launch a WifiSelect activity to let the user choose a
 * wifi network, and get back the selected network when the user is done.
 *
 * Main differences from Android's ActivityManager:
 * - No onPause/onResume, since we don't have a concept of background activities
 * - onActivityResult is implemented via a callback instead of a separate method, for simplicity
 */
class ActivityManager {
  friend class RenderLock;

 protected:
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;
  std::vector<std::unique_ptr<Activity>> stackActivities;
  std::unique_ptr<Activity> currentActivity;

  // The frontlight panel lives here rather than in any one activity: the top-edge swipe
  // that opens it works from every screen, and the band is drawn over whatever the last
  // render left in the framebuffer instead of replacing it.
  LightPanel lightPanel;
  std::atomic<bool> quickLocked{false};
  void paintQuickLockBadge();
  std::function<void()> onSleep_;

  void exitActivity(const RenderLock& lock);

  // Name of the screen the queued transition is heading for, for the busy strip's
  // label. A pop with an empty stack lands on Home, which is why this is not simply
  // pendingActivity->name.
  const std::string& pendingTransitionName() const;

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;
  // The queued pop was asked for on a pass that read Back (MenuReturn).
  bool poppedByBack = false;
  // MenuReturn for the light panel's tiles: reopen it after render pass lightPanelReopenAfter.
  static void reopenLightPanel(void* self, int);
  bool lightPanelReopen = false;
  uint32_t lightPanelReopenAfter = 0;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;
  uint32_t renderPassCompleted = 0;
  uint32_t waitingTargetPass = 0;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};

 public:
  GfxRenderer& getRenderer() { return renderer; }

  // Out of line: the unique_ptr<Activity> members need Activity complete to grow and destroy.
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ActivityManager();

  void begin();

  // The light panel's two actions. Sleep can only be run by main.cpp, so it is handed in;
  // Rotate is answered here, because whether an orientation means anything depends on the
  // screen on top.
  void setSleepAction(std::function<void()> onSleep);

  // The light panel asks for these every time it opens: what to put in its aux row and
  // its action grid, and what a step of that aux row means here.
  void buildLightPanelContext(light_panel::Context& context);
  bool stepLightPanelAux(int delta);
  bool stepLightPanelAux2(int delta);

  // One of the home screen's books, chosen at random, or empty when the list is.
  std::string randomBookPath();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // The screens a replace destroyed but Back must come back to (ActivityOpeners.cpp). A few
  // screens replace the one they were opened from instead of covering it, to give a radio
  // its RAM. They name that opener here, and when they close with nothing under them the
  // opener is rebuilt in place of Home. Home, and any plain replace, forgets them all.
  enum class Opener : uint8_t {
    SettingsFileTransfer,      // the Settings hub, on its File Transfer card
    SettingsShareCredentials,  // Settings > System, on Share Credentials
    FileBrowser,               // the browser, on `path`
    NearbyChoose,              // Home's File Transfer: the Nearby send / receive choice
    FileTransfer,              // the File Transfer mode list
    OpdsServers,               // the OPDS server picker
    Settings,                  // Settings on the card, category and row in `row` (restartStep)
    Reader,                    // the book at `path` (APP_STATE.openEpubPath when empty)
    KOReaderSettings,          // Settings > KOReader Sync, on `row` (restartStep)
  };
  // `row` is the opener's row that opened the new screen (the server, the mode).
  void replaceFromOpener(std::unique_ptr<Activity>&& newActivity, Opener opener, std::string path = {}, int row = 0);
  // Where Back from the screen on top lands, packed for a silent restart (BackTrail.h): the
  // openers, then the screens under it that can be rebuilt (Activity::restartStep), and with
  // `withTop` the screen on top itself (a reboot from its own row, Settings > Wi-Fi Networks).
  // followBackTrail() is the boot side: a radio screen's reboot comes back there, not Home.
  uint64_t backTrail(bool withTop = false) const;
  void followBackTrail(uint64_t trail);

  // The same for the screens that replace the open book (KOReader Sync, Nearby): the reader
  // hands over the menu that launched one (MenuReturn::armed(), empty for a bound key), and
  // Back from that screen reopens the book and then that menu, on its row. The reopen's ctx
  // is stale by then; the new reader passes itself.
  void rememberReaderMenu(const MenuReturn& menu) { readerMenu = menu; }
  MenuReturn takeReaderMenu() { return std::exchange(readerMenu, MenuReturn{}); }
  void goBackToReader(std::string path);

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToSettings();
  void goToFileBrowser(std::string path = {});
  void goToBrowser();
  // firstTurnCleans: the wake painted the first page with a drive-all FAST over the
  // sleep face, so the first page turn runs the clean pass (wake_face::firstPageTurnCleans).
  void goToReader(std::string path, bool allowFastInitialRefresh = false, bool firstTurnCleans = false,
                  bool sortesMode = false);
  // The reader's short Back: the screen the open book was launched from (APP_STATE.readerOrigin).
  void goToReaderOrigin();
  void goToSleep(bool fromTimeout = false);
  void goToBoot();
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE);

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool requiresExclusiveStorageLoop() const;
  bool isReaderActivity() const;
  bool isHomeActivity() const;
  bool handleForcedRefresh();
  // Forwarded to the CURRENT activity only — unlike isReaderActivity(), which is also true
  // while a child screen launched from the reader sits on top of it.

  // Per-button bindings. isBookContext() picks which of the two binding sets the router
  // arms; runBoundAction() offers the action to the screen on top first and falls back to
  // the ones that work anywhere (Home, Back, Frontlight). Sleep is not here: only main.cpp
  // can put the device to sleep.
  bool isBookContext() const;
  bool runBoundAction(uint8_t function);
  // Quick Lock: the screen stays up and ignores all input, only the bound gesture releases
  // it. Lives in memory, so any sleep ends it. Refused while the screen has work its loop
  // must keep running (a transfer) or the light panel is up.
  bool isQuickLocked() const { return quickLocked; }
  void toggleQuickLock();
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();

 private:
  // Declared after Opener, which they name.
  struct OpenerEntry {
    Opener opener;
    std::string path;
    int row = 0;
  };
  std::vector<OpenerEntry> openers;
  MenuReturn readerMenu;
  // replaceActivity() without forgetting the openers.
  void queueReplace(std::unique_ptr<Activity>&& newActivity);
  // Rebuilds the last opener in place of the screen that just closed. False when none.
  bool reopenOpener();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
