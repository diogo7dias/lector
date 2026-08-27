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
#include "components/LightPanel.h"
#include "util/ScreenshotInfo.h"

class Activity;    // forward declaration
class RenderLock;  // forward declaration

enum class HomeMenuItem {
  NONE,
  FILE_BROWSER,
  OPDS_BROWSER,
  FILE_TRANSFER,
  SETTINGS_MENU,
#ifdef LECTOR_LOCK_LAB_UI
  // Last on purpose, so the throwaway row cannot shift the index of a real one.
  LOCK_LAB,
#endif
};

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
  std::function<void()> onSleep_;

  void exitActivity(const RenderLock& lock);

  // Pending activity to be launched on next loop iteration
  std::unique_ptr<Activity> pendingActivity;
  enum class PendingAction { None, Push, Pop, Replace };
  PendingAction pendingAction = PendingAction::None;

  // Task to render and display the activity
  TaskHandle_t renderTaskHandle = nullptr;
  static void renderTaskTrampoline(void* param);
  [[noreturn]] virtual void renderTaskLoop();

  // Set by requestUpdateAndWait(); read and cleared by the render task after render completes.
  // Note: only one waiting task is supported at a time
  TaskHandle_t waitingTaskHandle = nullptr;

  // Mutex to protect rendering operations from race conditions
  // Must only be used via RenderLock
  SemaphoreHandle_t renderingMutex = nullptr;

  // Whether to trigger a render after the current loop()
  // This variable must only be set by the main loop, to avoid race conditions
  std::atomic<bool> requestedUpdate{false};

 public:
  explicit ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
    assert(renderingMutex != nullptr && "Failed to create rendering mutex");
    stackActivities.reserve(10);
  }
  ~ActivityManager() { assert(false); /* should never be called */ };

  void begin();

  // The light panel's two actions. Sleep can only be run by main.cpp, so it is handed in;
  // Rotate is answered here, because whether an orientation means anything depends on the
  // screen on top.
  void setSleepAction(std::function<void()> onSleep);

  // The light panel asks for these every time it opens: what to put in its aux row and
  // its action grid, and what a step of that aux row means here.
  void buildLightPanelContext(light_panel::Context& context);
  bool stepLightPanelAux(int delta);

  // One book chosen at random from the card, or empty when the card holds none.
  std::string randomBookPath();
  void loop();

  // Will replace currentActivity and drop all activities on stack
  void replaceActivity(std::unique_ptr<Activity>&& newActivity);

  // goTo... functions are convenient wrapper for replaceActivity()
  void goToFileTransfer();
  void goToSettings();
#ifdef LECTOR_LOCK_LAB_UI
  void goToLockLab();
#endif
  void goToFileBrowser(std::string path = {});
  void goToBrowser();
  void goToReader(std::string path, bool allowFastInitialRefresh = false);
  void goToSleep(bool fromTimeout = false);
  // wallpaperPath: see BootActivity — a .pxc wallpaper to unlock over, or empty for the
  // plain logo boot screen.
  void goToBoot(std::string wallpaperPath = {});
  void goToFullScreenMessage(std::string message, EpdFontFamily::Style style = EpdFontFamily::REGULAR);
  void goToCrashReport();
  void goHome(HomeMenuItem initialMenuItem = HomeMenuItem::NONE, bool cleanInitialRefresh = false);

  // This will move current activity to stack instead of deleting it
  void pushActivity(std::unique_ptr<Activity>&& activity);

  // Remove the currentActivity, returning the last one on stack
  // Note: if popActivity() on last activity on the stack, we will goHome()
  void popActivity();

  bool preventAutoSleep() const;
  bool isReaderActivity() const;
  // True when the home menu is the activity on screen. Used by the Quick Resume lock,
  // which repaints home before sleeping only when it is not already showing.
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
  bool skipLoopDelay() const;
  ScreenshotInfo getScreenshotInfo() const;

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  void requestUpdate(bool immediate = false);

  // Trigger a render and block until it completes.
  // Must NOT be called from the render task or while holding a RenderLock.
  void requestUpdateAndWait();
};

extern ActivityManager activityManager;  // singleton, to be defined in main.cpp
