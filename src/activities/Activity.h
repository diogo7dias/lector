#pragma once
#include <Logging.h>

#include <cassert>
#include <memory>
#include <string>
#include <utility>

#include "ActivityManager.h"  // for using the ActivityManager singleton
#include "ActivityResult.h"
#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "RenderLock.h"
#include "util/ScreenshotInfo.h"

class Activity {
  friend class ActivityManager;

 protected:
  std::string name;
  GfxRenderer& renderer;
  MappedInputManager& mappedInput;

  ActivityResultHandler resultHandler;
  ActivityResult result;

 public:
  explicit Activity(std::string name, GfxRenderer& renderer, MappedInputManager& mappedInput)
      : name(std::move(name)), renderer(renderer), mappedInput(mappedInput) {}
  virtual ~Activity() = default;
  virtual void onEnter();
  virtual void onExit();
  virtual void loop() {}

  virtual void render(RenderLock&&) {}

  // If immediate is true, the update will be triggered immediately.
  // Otherwise, it will be deferred until the end of the current loop iteration.
  virtual void requestUpdate(bool immediate = false);

  // Request an immediate render and block until it completes.
  virtual void requestUpdateAndWait();

  virtual bool skipLoopDelay() { return false; }
  virtual bool preventAutoSleep() { return false; }
  virtual bool isReaderActivity() const { return false; }
  // True for the reading surfaces night mode inverts (EPUB/TXT/XTC). Resolved
  // per render by ActivityManager, so menus, overlays, and every other screen
  // keep normal polarity without managing the display flag themselves.
  virtual bool appliesNightMode() const { return false; }
  // Returns true when the activity schedules its own forced refresh.
  virtual bool handleForcedRefresh() { return false; }

  // Power double-click. Only the EPUB reader answers these, and only while it is the
  // activity on top: main.cpp arms the detector from wantsPowerDoubleClick() alone, so a
  // child screen opened from the book (dictionary, chapter list) never pays the delay.
  virtual bool wantsPowerDoubleClick() const { return false; }
  virtual void runPowerDoubleClick() {}

  // Per-button bindings (Settings > Controls > Buttons). The router picks the in-book set
  // of bindings while a book is open, so the same key can page a book and open the light
  // panel on the home screen.
  virtual bool isBookContext() const { return false; }
  // Run a bound action on this screen. False means "not consumed": either this screen
  // cannot run it, or it is bound but impossible right now (no footnote on the page, no
  // KOReader credentials). ActivityManager then tries the actions that work anywhere.
  virtual bool runBoundAction(uint8_t function) {
    (void)function;
    return false;
  }
  virtual bool isHomeActivity() const { return false; }
  // The Home gesture (the capacitive Home key, or a bottom-edge up-swipe on boards
  // without one) pops to Home from anywhere. An activity that must do something
  // else first — save, confirm, leave a sub-mode — overrides this and returns true
  // to keep the pop from happening.
  virtual bool handleHomeGesture() { return false; }
  virtual ScreenshotInfo getScreenshotInfo() const { return {}; }

  // Start a new activity without destroying the current one
  // Note: requestUpdate() will be invoked automatically once resultHandler finishes
  void startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler);

  // Set the result to be passed back to the previous activity when this activity finishes
  void setResult(ActivityResult&& result);

  // Finish this activity and return to the previous one on the stack (if any)
  void finish();

  // Convenience method to facilitate API transition to ActivityManager
  // TODO: remove this in near future
  void onGoHome(HomeMenuItem item = HomeMenuItem::NONE);
  void onSelectBook(const std::string& path);
};
