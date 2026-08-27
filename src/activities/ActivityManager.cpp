#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <PerfLog.h>
#include <PerfStats.h>
#include <esp_random.h>

#include <algorithm>
#include <cstring>
#include <string_view>

#include "CrossPointSettings.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "components/BusyBanner.h"
#include "components/RowHitTest.h"
#include "dev/LockLabActivity.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/BusyTick.h"
#include "util/DebugTrace.h"
#include "util/FullScreenMessageActivity.h"
#include "util/OrientationCycle.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

namespace {

// The Sort row's current value, for the light panel's aux row outside a book.
StrId bookOrderLabel(const uint8_t order) {
  switch (order) {
    case CrossPointSettings::BOOK_ORDER_RANDOM:
      return StrId::STR_BOOK_ORDER_RANDOM;
    case CrossPointSettings::BOOK_ORDER_RECENTLY_ADDED:
      return StrId::STR_BOOK_ORDER_RECENTLY_ADDED;
    case CrossPointSettings::BOOK_ORDER_LAST_READ:
      return StrId::STR_BOOK_ORDER_LAST_READ;
    default:
      return StrId::STR_BOOK_ORDER_ALPHABETICAL;
  }
}

// What the panel's action grid holds. Refresh and Rotate work anywhere, so they lead both
// sets; the rest is what you would want where you are. In a book that is the two things
// about this book you change most; outside one it is getting back into a book, or finding
// a different one.
constexpr uint8_t IN_BOOK_ACTIONS[] = {CrossPointSettings::LP_MENU_FORCE_REFRESH,
                                       CrossPointSettings::LP_MENU_ROTATE,
                                       CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR,
                                       CrossPointSettings::LP_MENU_READER_SETTINGS};

constexpr uint8_t OUT_OF_BOOK_ACTIONS[] = {
    CrossPointSettings::LP_MENU_FORCE_REFRESH, CrossPointSettings::LP_MENU_ROTATE,
    CrossPointSettings::LP_MENU_CONTINUE_READING, CrossPointSettings::LP_MENU_RANDOM_BOOK,
    CrossPointSettings::LP_MENU_SEARCH, CrossPointSettings::LP_MENU_SETTINGS};

// One book chosen at random from the card, or empty when there are none.
//
// A reservoir sample over a bounded walk: the whole point is one press and one book, and
// building a list of every book on the card first would cost the memory and the time that
// the file browser already spends when you want to choose for yourself. Caps exist because
// this runs on the loop task with a busy banner over it, not because a bigger card is
// wrong — a card past the cap simply picks from the first 4000 books it meets.
constexpr int RANDOM_BOOK_MAX_DEPTH = 4;
constexpr uint32_t RANDOM_BOOK_MAX_BOOKS = 4000;
// The walk is bounded by what it reads, not only by what it finds: a folder of ten
// thousand images holds no books at all, and without this the book cap would never
// be reached while the card was read from end to end.
constexpr uint32_t RANDOM_BOOK_MAX_SCANNED = 20000;

struct RandomBookScan {
  uint32_t books = 0;
  uint32_t scanned = 0;
  std::string picked;

  bool exhausted() const { return books >= RANDOM_BOOK_MAX_BOOKS || scanned >= RANDOM_BOOK_MAX_SCANNED; }
};

void walkForRandomBook(const std::string& path, const int depth, RandomBookScan& scan) {
  if (depth > RANDOM_BOOK_MAX_DEPTH || scan.exhausted()) return;
  auto dir = Storage.open(path.c_str());
  if (!dir || !dir.isDirectory()) return;
  dir.rewindDirectory();

  char name[256];
  for (auto entry = dir.openNextFile(); entry; entry = dir.openNextFile()) {
    if (scan.exhausted()) break;
    // Blocks the loop task, so let the busy banner appear and keep the watchdog fed.
    if ((++scan.scanned & 0x3F) == 0) busy::tick();
    entry.getName(name, sizeof(name));
    if (name[0] == '.' || strcmp(name, "System Volume Information") == 0) continue;

    const std::string child = path == "/" ? std::string("/") + name : path + "/" + name;
    if (entry.isDirectory()) {
      // The wallpaper folders hold thousands of images and no books.
      if (child == "/sleep" || child == "/sleep pause") continue;
      entry.close();
      walkForRandomBook(child, depth + 1, scan);
      continue;
    }
    const std::string_view filename{name};
    if (!FsHelpers::hasEpubExtension(filename) && !FsHelpers::hasXtcExtension(filename) &&
        !FsHelpers::hasTxtExtension(filename) && !FsHelpers::hasMarkdownExtension(filename)) {
      continue;
    }
    // Reservoir sampling: the nth book seen replaces the pick with probability 1/n, which
    // leaves every book equally likely without knowing how many there are.
    ++scan.books;
    if (esp_random() % scan.books == 0) scan.picked = child;
  }
  dir.close();
}

}  // namespace

std::string ActivityManager::randomBookPath() {
  BusyBanner banner(renderer, tr(STR_BUSY_READING_FOLDER));
  RandomBookScan scan;
  walkForRandomBook("/", 0, scan);
  return std::move(scan.picked);
}

void ActivityManager::setSleepAction(std::function<void()> onSleep) {
  onSleep_ = std::move(onSleep);
  lightPanel.setHost([this](light_panel::Context& context) { buildLightPanelContext(context); },
                     [this](const uint8_t function) { return runBoundAction(function); },
                     [this](const int delta) { return stepLightPanelAux(delta); });
}

void ActivityManager::buildLightPanelContext(light_panel::Context& context) {
  // The screen on top names its own aux row if it has one (Text Size in a book). Outside a
  // book the browser order is the value worth a stepper, and it belongs to the setting
  // rather than to any one screen, so it is filled in here.
  const bool taken = currentActivity && currentActivity->lightPanelAuxText(context.auxText, sizeof(context.auxText));
  if (!taken && !isBookContext()) {
    snprintf(context.auxText, sizeof(context.auxText), "%s: %s", I18N.get(StrId::STR_SORT),
             I18N.get(bookOrderLabel(SETTINGS.bookBrowserOrder)));
  }

  const bool inBook = isBookContext();
  const uint8_t* actions = inBook ? IN_BOOK_ACTIONS : OUT_OF_BOOK_ACTIONS;
  const int count = inBook ? static_cast<int>(std::size(IN_BOOK_ACTIONS)) : static_cast<int>(std::size(OUT_OF_BOOK_ACTIONS));
  context.actionCount = std::min(count, light_panel::kMaxActions);
  for (int i = 0; i < context.actionCount; ++i) context.actions[i] = actions[i];
}

bool ActivityManager::stepLightPanelAux(const int delta) {
  if (currentActivity && currentActivity->lightPanelStepAux(delta)) return true;
  if (isBookContext()) return false;

  const int count = CrossPointSettings::BOOK_ORDER_COUNT;
  const int next = (static_cast<int>(SETTINGS.bookBrowserOrder) + delta % count + count) % count;
  if (next == SETTINGS.bookBrowserOrder) return false;
  SETTINGS.bookBrowserOrder = static_cast<uint8_t>(next);
  SETTINGS.saveToFile();
  // The browser is the only screen laid out against this, and it has to re-read the folder
  // rather than re-sort what it drew: Last Read reads a key per book off the card.
  if (currentActivity) currentActivity->onBookOrderChanged();
  return true;
}

void ActivityManager::begin() {
#if defined(configNUM_CORES) && configNUM_CORES > 1
  constexpr BaseType_t renderTaskCore = 1;
#else
  constexpr BaseType_t renderTaskCore = 0;
#endif
  xTaskCreatePinnedToCore(&renderTaskTrampoline, "ActivityManagerRender",
                          8192,               // Stack size
                          this,               // Parameters
                          1,                  // Priority
                          &renderTaskHandle,  // Task handle
                          renderTaskCore  // Keep long renders/cover decodes off CPU 0's idle watchdog when available
  );
  assert(renderTaskHandle != nullptr && "Failed to create render task");
}

void ActivityManager::renderTaskTrampoline(void* param) {
  auto* self = static_cast<ActivityManager*>(param);
  self->renderTaskLoop();
}

void ActivityManager::renderTaskLoop() {
  while (true) {
    // Takes the whole pending count, not one notification: every update request that
    // arrived while the previous refresh was on the panel is served by this single pass,
    // which is what keeps a held-down button from costing one refresh per press. The
    // count is recorded so a log can say how much actually collapsed.
    const uint32_t requestsServed = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    PerfStats::noteRenderPass(requestsServed);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      HalPowerManager::Lock powerLock;  // Ensure we don't go into low-power mode while rendering
      // Night mode inverts only the reading surfaces (appliesNightMode):
      // resolving the output polarity here, per render, means menus, popups,
      // and every other activity revert to normal automatically.
      display.setInverted(SETTINGS.screenInverted != 0 && currentActivity->appliesNightMode());
      // A tap answers to what is on screen now, so the row table starts empty on every
      // paint and each list draw appends the rows it actually painted. Cleared here, the
      // one place a paint begins, rather than inside the list draws — a screen that draws
      // two lists (the home's books and its menu) would otherwise have the second wipe the
      // first, and a screen that draws none would leave the previous screen's rows live.
      if (lightPanel.isActive()) {
        // Deliberately not re-rendering the activity: the panel is drawn over the page
        // already in the framebuffer, which is what makes it a live preview of the light
        // rather than a screen you leave the book for.
        lightPanel.processRender(renderer);
      } else {
        row_hit::lastRows().begin();
        currentActivity->render(std::move(lock));
      }
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    waiter = waitingTaskHandle;
    waitingTaskHandle = nullptr;
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (currentActivity) {
    // Home from anywhere, handled once here rather than in every activity: the
    // capacitive Home key on boards that have one, the bottom-edge up-swipe on the
    // rest. Activities that need to intervene override handleHomeGesture().
    // The light panel takes input before anything else while it is up, and the top-edge
    // swipe opens it from any screen. Handled once here for the same reason Home is.
    // Immediate, not deferred. Every branch below returns out of loop() before the
    // deferred-update flush at the bottom of this function, and once the panel is up
    // handleInput() consumes the pass on every pass, so a deferred request would never
    // be flushed at all: the panel opened, reported present=1, and was never drawn.
    if (lightPanel.isActive()) {
      if (lightPanel.handleInput(mappedInput, [this] { requestUpdate(/*immediate=*/true); })) return;
    } else if (mappedInput.wasMenuGesture()) {
      // Logged rather than silently skipped: a board with no frontlight and a swipe that
      // never decoded look the same from the outside, and only the log separates them.
      if (!Frontlight.present()) {
        debug_trace::note("top-edge gesture ignored: no frontlight on this board");
      } else {
        debug_trace::note("top-edge gesture: opening the light panel");
        lightPanel.show();
        requestUpdate(/*immediate=*/true);
        return;
      }
    }

    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture()) {
      if (currentActivity->handleHomeGesture()) return;
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  while (pendingAction != PendingAction::None) {
    if (pendingAction == PendingAction::Pop) {
      RenderLock lock;

      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        LOG_DBG("ACT", "No more activities on stack, going home");
        lock.unlock();  // goHome may acquire its own lock
        goHome();
        continue;  // Will launch goHome immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        // Re-tag the perf log. onEnter() is what normally sets this, and a resumed
        // activity is never re-entered — it was pushed under, not exited — so without
        // this every refresh after a child screen closes is still filed under the child.
        // That is how a reader page turn came back labelled "TextSettings".
        PerfLog::flush();
        PerfLog::setScreen(currentActivity->name.c_str());
        // Handle result if necessary
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }

        // Request an update to ensure the popped activity gets re-rendered
        if (pendingAction == PendingAction::None) {
          requestUpdate();
        }

        // Handler may request another pending action, we will handle it in the next loop iteration
        continue;
      }

    } else if (pendingActivity) {
      // Current activity has requested a new activity to be launched
      RenderLock lock;

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        // Clear the stack
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        // Move current activity to stack
        stackActivities.push_back(std::move(currentActivity));
        LOG_DBG("ACT", "Pushed to activity stack, new size = %zu", stackActivities.size());
      }
      pendingAction = PendingAction::None;
      currentActivity = std::move(pendingActivity);

      lock.unlock();  // onEnter may acquire its own lock
      currentActivity->onEnter();

      // onEnter may request another pending action, we will handle it in the next loop iteration
      continue;
    }
  }

  if (requestedUpdate.exchange(false)) {
    // Using direct notification to signal the render task to update
    // Increment counter so multiple rapid calls won't be lost
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  }
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  // Note: no lock here, this is usually called by loop() and we may run into deadlock
  if (currentActivity) {
    // Defer launch if we're currently in an activity, to avoid deleting the current activity
    // leading to the "delete this" problem
    pendingActivity = std::move(newActivity);
    pendingAction = PendingAction::Replace;
  } else {
    // No current activity, safe to launch immediately
    currentActivity = std::move(newActivity);
    currentActivity->onEnter();
  }
}

void ActivityManager::goToFileTransfer() {
  replaceActivity(std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput));
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

#ifdef LECTOR_LOCK_LAB_UI
void ActivityManager::goToLockLab() { replaceActivity(std::make_unique<LockLabActivity>(renderer, mappedInput)); }
#endif

void ActivityManager::goToFileBrowser(std::string path) {
  replaceActivity(std::make_unique<FileBrowserActivity>(renderer, mappedInput, std::move(path)));
}

void ActivityManager::goToBrowser() {
  const auto& servers = OPDS_STORE.getServers();
  // Skip the server picker when there's only one server configured
  if (servers.size() == 1) {
    replaceActivity(std::make_unique<OpdsBookBrowserActivity>(renderer, mappedInput, servers[0]));
  } else {
    replaceActivity(std::make_unique<OpdsServerListActivity>(renderer, mappedInput, true));
  }
}

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh) {
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh));
}

void ActivityManager::goToSleep(bool fromTimeout) {
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot(std::string wallpaperPath) {
  replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput, std::move(wallpaperPath)));
}

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem, bool cleanInitialRefresh) {
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) {
    const auto& activityName = currentActivity->name;
    if (activityName == "FileBrowser") {
      initialMenuItem = HomeMenuItem::FILE_BROWSER;
    } else if (activityName == "OpdsBookBrowser") {
      initialMenuItem = HomeMenuItem::OPDS_BROWSER;
    } else if (activityName == "CrossPointWebServer") {
      initialMenuItem = HomeMenuItem::FILE_TRANSFER;
    } else if (activityName == "Settings") {
      initialMenuItem = HomeMenuItem::SETTINGS_MENU;
    }
  }
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem, cleanInitialRefresh));
}
void ActivityManager::goToCrashReport() { replaceActivity(std::make_unique<CrashActivity>(renderer, mappedInput)); }

void ActivityManager::pushActivity(std::unique_ptr<Activity>&& activity) {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while pushActivity is not expected");
    pendingActivity.reset();
  }
  pendingActivity = std::move(activity);
  pendingAction = PendingAction::Push;
}

void ActivityManager::popActivity() {
  if (pendingActivity) {
    // Should never happen in practice
    LOG_ERR("ACT", "pendingActivity while popActivity is not expected");
    pendingActivity.reset();
  }
  pendingAction = PendingAction::Pop;
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::isHomeActivity() const { return currentActivity && currentActivity->name == "Home"; }

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::isBookContext() const { return currentActivity && currentActivity->isBookContext(); }

bool ActivityManager::runBoundAction(const uint8_t function) {
  debug_trace::note("bound action %u dispatched", function);
  // The screen on top gets first refusal: in a book, most of these actions are its own.
  if (currentActivity && currentActivity->runBoundAction(function)) {
    debug_trace::note("bound action %u taken by the activity", function);
    return true;
  }

  switch (function) {
    case CrossPointSettings::LP_MENU_GO_HOME:
      if (currentActivity && currentActivity->isHomeActivity()) return false;
      goHome();
      return true;
    case CrossPointSettings::LP_MENU_BACK:
      popActivity();
      return true;
    case CrossPointSettings::LP_MENU_SLEEP:
      if (!onSleep_) return false;
      onSleep_();
      return true;
    case CrossPointSettings::LP_MENU_ROTATE: {
      const uint8_t orientation = orientation_cycle::next(SETTINGS.orientation);
      // The reader owns this: it persists the setting and re-indexes the chapter at the
      // new column width. Anywhere else there is nothing laid out against an orientation,
      // so the setting is simply stored and the next book opens turned.
      if (currentActivity && currentActivity->applyReaderOrientation(orientation)) return true;
      SETTINGS.orientation = orientation;
      SETTINGS.saveToFile();
      renderer.setOrientation(static_cast<GfxRenderer::Orientation>(SETTINGS.orientation));
      requestUpdate(/*immediate=*/true);
      return true;
    }
    case CrossPointSettings::LP_MENU_CONTINUE_READING: {
      const auto& books = RECENT_BOOKS.getBooks();
      // Nothing read yet, or the card no longer holds it: a dead press is better than an
      // error screen from a panel you opened to change the light.
      if (books.empty() || !Storage.exists(books.front().path.c_str())) return false;
      goToReader(books.front().path);
      return true;
    }
    case CrossPointSettings::LP_MENU_RANDOM_BOOK: {
      std::string picked = randomBookPath();
      if (picked.empty()) return false;
      goToReader(std::move(picked));
      return true;
    }
    case CrossPointSettings::LP_MENU_SEARCH:
      // The browser owns Search and answered above if it is the screen on top. From
      // anywhere else the search has no folder to run in, so this opens the one it does.
      goHome(HomeMenuItem::FILE_BROWSER);
      return true;
    case CrossPointSettings::LP_MENU_SETTINGS:
      goHome(HomeMenuItem::SETTINGS_MENU);
      return true;
    case CrossPointSettings::LP_MENU_LIGHT_PANEL:
      if (!Frontlight.present() || lightPanel.isActive()) {
        debug_trace::note("light panel refused: present=%d active=%d", Frontlight.present() ? 1 : 0,
                          lightPanel.isActive() ? 1 : 0);
        return false;
      }
      lightPanel.show();
      // Immediate for the same reason the gesture path is: ActivityManager::loop() will
      // hand the very next pass to the panel and return before its own flush.
      requestUpdate(/*immediate=*/true);
      return true;
    default:
      return false;
  }
}

bool ActivityManager::skipLoopDelay() const { return currentActivity && currentActivity->skipLoopDelay(); }

ScreenshotInfo ActivityManager::getScreenshotInfo() const {
  if (currentActivity) {
    return currentActivity->getScreenshotInfo();
  }
  return {};
}

void ActivityManager::requestUpdate(bool immediate) {
  if (immediate) {
    if (renderTaskHandle) {
      xTaskNotify(renderTaskHandle, 1, eIncrement);
    }
  } else {
    // Deferring the update until current loop is finished
    // This is to avoid multiple updates being requested in the same loop
    requestedUpdate = true;
  }
}
void ActivityManager::requestUpdateAndWait() {
  if (!renderTaskHandle) {
    return;
  }

  // Atomic section to perform checks
  taskENTER_CRITICAL(&activityManagerSpinlock);
  auto currTaskHandler = xTaskGetCurrentTaskHandle();
  auto mutexHolder = xSemaphoreGetMutexHolder(renderingMutex);
  bool isRenderTask = (currTaskHandler == renderTaskHandle);
  bool alreadyWaiting = (waitingTaskHandle != nullptr);
  bool holdingRenderLock = (mutexHolder == currTaskHandler);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  // Tell the power manager the loop is parked here: it cannot poll input until the
  // render finishes, so the BUSY-wait slice hook should not yield to it meanwhile.
  powerManager.noteRenderWaitBegin();
  ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
  powerManager.noteRenderWaitEnd();
}

// RenderLock

RenderLock::RenderLock() {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::RenderLock([[maybe_unused]] Activity&) {
  xSemaphoreTake(activityManager.renderingMutex, portMAX_DELAY);
  isLocked = true;
}

RenderLock::~RenderLock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

void RenderLock::unlock() {
  if (isLocked) {
    xSemaphoreGive(activityManager.renderingMutex);
    isLocked = false;
  }
}

/**
 *
 * Checks if renderingMutex is busy.
 *
 * @return true if renderingMutex is busy, otherwise false.
 *
 */
bool RenderLock::peek() { return xQueuePeek(activityManager.renderingMutex, NULL, 0) != pdTRUE; };
