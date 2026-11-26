#include "ActivityManager.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <HalDisplay.h>
#include <HalPowerManager.h>
#include <I18n.h>
#include <Memory.h>
#include <esp_random.h>

#include <algorithm>
#include <optional>

#include "Activity.h"
#include "ActivityBusyLabel.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "OpdsServerStore.h"
#include "RecentBooksStore.h"
#include "boot_sleep/BootActivity.h"
#include "boot_sleep/SleepActivity.h"
#include "browser/OpdsBookBrowserActivity.h"
#include "components/BusyBanner.h"
#include "components/HintBandGeometry.h"
#include "components/RowHitTest.h"
#include "fontIds.h"
#include "frontlight/ApplyFrontlightSetting.h"
#include "home/CrashActivity.h"
#include "home/FileBrowserActivity.h"
#include "home/HomeActivity.h"
#include "home/LibraryActivity.h"
#include "network/CrossPointWebServerActivity.h"
#include "reader/ReaderActivity.h"
#include "settings/OpdsServerListActivity.h"
#include "settings/SettingsActivity.h"
#include "util/BookOrderLabel.h"
#include "util/FullScreenMessageActivity.h"
#include "util/OrientationCycle.h"

static portMUX_TYPE activityManagerSpinlock = portMUX_INITIALIZER_UNLOCKED;

ActivityManager::ActivityManager(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : renderer(renderer), mappedInput(mappedInput), renderingMutex(xSemaphoreCreateMutex()) {
  assert(renderingMutex != nullptr && "Failed to create rendering mutex");
  stackActivities.reserve(10);
}

ActivityManager::~ActivityManager() { assert(false); /* should never be called */ }

namespace {

// What the panel's action grid holds. Refresh and Rotate work anywhere, so they lead both
// sets; the rest is what you would want where you are. In a book that is the two things
// about this book you change most; outside one it is getting back into a book, or finding
// a different one. Alignment and paragraph spacing close the in-book set, the alignment
// pair side by side so it reads as one choice.
constexpr uint8_t IN_BOOK_ACTIONS[] = {CrossPointSettings::LP_MENU_FORCE_REFRESH,
                                       CrossPointSettings::LP_MENU_ROTATE,
                                       CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR,
                                       CrossPointSettings::LP_MENU_READER_SETTINGS,
                                       CrossPointSettings::LP_MENU_ALIGN_LEFT,
                                       CrossPointSettings::LP_MENU_ALIGN_JUSTIFY,
                                       CrossPointSettings::LP_MENU_TOGGLE_PARAGRAPH_SPACING};

constexpr uint8_t OUT_OF_BOOK_ACTIONS[] = {
    CrossPointSettings::LP_MENU_FORCE_REFRESH,    CrossPointSettings::LP_MENU_ROTATE,
    CrossPointSettings::LP_MENU_CONTINUE_READING, CrossPointSettings::LP_MENU_RANDOM_BOOK,
    CrossPointSettings::LP_MENU_SEARCH,           CrossPointSettings::LP_MENU_SETTINGS};

}  // namespace

// One of the books on the home screen, chosen at random, or empty when the list is.
//
// The recents list, not the card: the panel is a place you reach mid-page, and a walk of
// every folder on a full card is seconds of work under a busy banner. These are also the
// books the home screen already offers, so the button shuffles what you can see rather
// than surfacing something you last opened two years ago.
std::string ActivityManager::randomBookPath() {
  const auto& books = RECENT_BOOKS.getBooks();
  // Missing files are skipped rather than picked and then refused: a card that lost one
  // book would otherwise make the button dead one press in thirteen.
  std::vector<const std::string*> present;
  present.reserve(books.size());
  for (const auto& book : books) {
    if (Storage.exists(book.path.c_str())) present.push_back(&book.path);
  }
  if (present.empty()) return {};
  return *present[esp_random() % present.size()];
}

void ActivityManager::setSleepAction(std::function<void()> onSleep) {
  onSleep_ = std::move(onSleep);
  lightPanel.setHost([this](light_panel::Context& context) { buildLightPanelContext(context); },
                     [this](const uint8_t function) {
                       // A screen the tile opens comes back to the panel on Back.
                       MenuReturn::Scope backToPanel(&ActivityManager::reopenLightPanel, this, 0);
                       return runBoundAction(function);
                     },
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
  const int count =
      inBook ? static_cast<int>(std::size(IN_BOOK_ACTIONS)) : static_cast<int>(std::size(OUT_OF_BOOK_ACTIONS));
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
    // which is what keeps a held-down button from costing one refresh per press.
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    // Acquire the lock before reading currentActivity to avoid a TOCTOU race
    // where the main task deletes the activity between the null-check and render().
    RenderLock lock;
    if (currentActivity) {
      if (!renderer.hasFrameBuffer()) {
        LOG_DBG("ACT", "Skipping render: framebuffer is currently lent out");
        requestedUpdate = true;
      } else {
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
        if (quickLocked) {
          paintQuickLockBadge();
        } else if (lightPanel.isActive()) {
          // Deliberately not re-rendering the activity: the panel is drawn over the page
          // already in the framebuffer, which is what makes it a live preview of the light
          // rather than a screen you leave the book for.
          lightPanel.processRender(renderer);
        } else {
          row_hit::lastRows().begin();
          // Same contract for the button hints: a screen that paints none (a reader
          // page) must not keep answering to the band the screen before it drew, or a
          // tap along the bottom of the page is a Back that leaves the book.
          hint_band::lastPainted().valid = false;
          currentActivity->render(std::move(lock));
        }
      }
    }
    // Notify any task blocked in requestUpdateAndWait() that the render is done.
    TaskHandle_t waiter = nullptr;
    taskENTER_CRITICAL(&activityManagerSpinlock);
    renderPassCompleted++;
    if (waitingTaskHandle && renderPassCompleted >= waitingTargetPass) {
      waiter = waitingTaskHandle;
      waitingTaskHandle = nullptr;
      waitingTargetPass = 0;
    }
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (waiter) {
      xTaskNotify(waiter, 1, eIncrement);
    }
  }
}

void ActivityManager::loop() {
  if (requiresExclusiveStorageLoop()) {
    currentActivity->loop();
    if (requestedUpdate.exchange(false) && renderTaskHandle) xTaskNotify(renderTaskHandle, 1, eIncrement);
    return;
  }

  // The one gate: every screen, the light panel and the Home gesture read their input below.
  if (quickLocked) {
    if (requestedUpdate.exchange(false) && renderTaskHandle) xTaskNotify(renderTaskHandle, 1, eIncrement);
    return;
  }

  // Back from a screen a panel tile opened: the panel again, once the screen under it has
  // been drawn (the panel paints over the framebuffer, which still held the closed screen).
  if (lightPanelReopen && pendingAction == PendingAction::None && renderPassCompleted >= lightPanelReopenAfter) {
    lightPanelReopen = false;
    lightPanel.show();
    requestUpdate(/*immediate=*/true);
    return;
  }

  if (currentActivity) {
    // Home from anywhere, handled once here rather than in every activity: the
    // capacitive Home key goes Home everywhere, and outside a book the bottom-edge
    // up-swipe does too (inside a book that swipe opens the reader menu).
    // The light panel takes input before anything else while it is up, and the top-edge
    // swipe opens it from any screen. Handled once here for the same reason Home is.
    // Immediate, not deferred. Every branch below returns out of loop() before the
    // deferred-update flush at the bottom of this function, and once the panel is up
    // handleInput() consumes the pass on every pass, so a deferred request would never
    // be flushed at all: the panel opened, reported present=1, and was never drawn.
    if (lightPanel.isActive()) {
      // close() clears isActive() before this runs: the page under the band gets a clean paint.
      if (lightPanel.handleInput(mappedInput, [this] {
            if (!lightPanel.isActive()) currentActivity->onRevealed();
            requestUpdate(/*immediate=*/true);
          }))
        return;
    } else if (mappedInput.wasMenuGesture()) {
      lightPanel.show();
      requestUpdate(/*immediate=*/true);
      return;
    }

    if (!currentActivity->isHomeActivity() && mappedInput.wasHomeGesture(/*allowBottomSwipe=*/!isBookContext())) {
      goHome();
      return;
    }

    // Note: do not hold a lock here, the loop() method must be responsible for acquire one if needed
    currentActivity->loop();
  }

  // A screen change ends the press that caused it. Screens disagree on which edge acts
  // (SettingsActivity opens on the press, UiListActivity activates a row on the release),
  // so without this the lift of the press that opened a screen fires again on the screen
  // it opened: Settings jumped into KOReader Sync and straight on into the Username
  // keyboard, on one press.
  if (pendingAction != PendingAction::None) mappedInput.suppressHeldButtonRelease();

  // One strip for the whole transition, armed and not drawn. A screen change that
  // completes quickly costs nothing; one that spends its time in onExit(), in a
  // constructor or in onEnter() gets the strip the moment it drags past the
  // banner's delay, which is the only way the user learns the press was taken.
  // Screens with their own slow steps still nest their own banners underneath.
  std::optional<BusyBanner> transitionBanner;
  if (pendingAction != PendingAction::None) {
    const std::string& destination = pendingTransitionName();
    if (activity_busy::wantsBanner(destination)) {
      transitionBanner.emplace(renderer, I18n::getInstance().get(activity_busy::labelFor(destination)));
    }
  }

  while (pendingAction != PendingAction::None) {
    RenderLock lock;
    // Row numbers belong to the outgoing screen, including when resuming a parent.
    row_hit::lastRows().begin();
    if (pendingAction == PendingAction::Pop) {
      if (!currentActivity) {
        // Should never happen in practice
        LOG_ERR("ACT", "Pop set but currentActivity is null; ignoring pop request");
        pendingAction = PendingAction::None;
        continue;
      }

      ActivityResult pendingResult = std::move(currentActivity->result);
      // Read before the screen is destroyed: a last screen popping lands Home on its row.
      const HomeMenuItem homeRow = homeRowFor(currentActivity->name);

      // Destroy the current activity
      exitActivity(lock);
      pendingAction = PendingAction::None;

      if (stackActivities.empty()) {
        lock.unlock();  // goHome may acquire its own lock
        // A screen that replaced its opener goes back there; anything else lands Home.
        if (!reopenOpener()) {
          LOG_DBG("ACT", "No more activities on stack, going home");
          goHome(homeRow);
        }
        continue;  // Will launch the next screen immediately

      } else {
        currentActivity = std::move(stackActivities.back());
        stackActivities.pop_back();
        LOG_DBG("ACT", "Popped from activity stack, new size = %zu", stackActivities.size());
        currentActivity->onRevealed();
        // Taken before the handler: a screen the handler opens arms its own.
        const MenuReturn backToMenu = std::exchange(currentActivity->menuReturn, MenuReturn{});
        if (currentActivity->resultHandler) {
          LOG_DBG("ACT", "Handling result for popped activity");

          // Move it here to avoid the case where handler calling another startActivityForResult()
          auto handler = std::move(currentActivity->resultHandler);
          currentActivity->resultHandler = nullptr;
          lock.unlock();  // Handler may acquire its own lock
          handler(pendingResult);
        }
        // Back from a screen a menu row opened lands on that menu again, on that row. Any
        // other close (a chapter picked, a Yes) stays where the handler left it, and so does
        // one whose handler already moved on.
        if (backToMenu && pendingAction == PendingAction::None && (pendingResult.isCancelled || poppedByBack)) {
          lock.unlock();
          backToMenu.run();
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

      if (pendingAction == PendingAction::Replace) {
        // Destroy the current activity
        exitActivity(lock);
        while (!stackActivities.empty()) {
          stackActivities.back()->onExit();
          stackActivities.pop_back();
        }
      } else if (pendingAction == PendingAction::Push) {
        currentActivity->onCovered();
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

const std::string& ActivityManager::pendingTransitionName() const {
  static const std::string kHome = activity_name::kHome;
  if (pendingActivity) return pendingActivity->name;
  if (!stackActivities.empty()) return stackActivities.back()->name;
  return kHome;
}

void ActivityManager::exitActivity(const RenderLock& lock) {
  // Note: lock must be held by the caller
  if (currentActivity) {
    currentActivity->onExit();
    currentActivity.reset();
  }
}

void ActivityManager::replaceActivity(std::unique_ptr<Activity>&& newActivity) {
  openers.clear();
  queueReplace(std::move(newActivity));
}

void ActivityManager::reopenLightPanel(void* self, int) {
  auto* manager = static_cast<ActivityManager*>(self);
  manager->lightPanelReopen = true;
  // The pass that paints the revealed screen: the next one, or the one after when the render
  // task is still busy with an old frame (as requestUpdateAndWait() counts).
  const bool busy = xSemaphoreGetMutexHolder(manager->renderingMutex) == manager->renderTaskHandle;
  manager->lightPanelReopenAfter = manager->renderPassCompleted + (busy ? 2 : 1);
}

void ActivityManager::queueReplace(std::unique_ptr<Activity>&& newActivity) {
  lightPanelReopen = false;  // a new screen: nothing to come back to
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

bool ActivityManager::requiresExclusiveStorageLoop() const {
  return currentActivity && currentActivity->requiresExclusiveStorageLoop();
}

void ActivityManager::goToSettings() { replaceActivity(std::make_unique<SettingsActivity>(renderer, mappedInput)); }

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

void ActivityManager::goToReader(std::string path, const bool allowFastInitialRefresh, const bool firstTurnCleans,
                                 const bool sortesMode) {
  // Where the book's short Back returns: the browser or the Library when it was picked
  // there, Home from anywhere else. The reader reopening itself (a sync, a transfer, a jump
  // to another book) and the boot that resumes it keep the origin they already have.
  if (currentActivity) {
    const std::string& launcher = currentActivity->name;
    if (launcher == activity_name::kFileBrowser) {
      APP_STATE.readerOrigin = CrossPointState::ReaderOrigin::FileBrowser;
    } else if (launcher == activity_name::kLibrary) {
      APP_STATE.readerOrigin = CrossPointState::ReaderOrigin::Library;
    } else if (!isReaderActivity() && launcher != activity_name::kBoot && launcher != activity_name::kSleep &&
               launcher != activity_name::kKOReaderSync && launcher != activity_name::kNearbyFileTransfer) {
      APP_STATE.readerOrigin = CrossPointState::ReaderOrigin::Home;
    }
  }
  readerMenu = MenuReturn{};
  replaceActivity(std::make_unique<ReaderActivity>(renderer, mappedInput, std::move(path), allowFastInitialRefresh,
                                                   firstTurnCleans, sortesMode));
}

void ActivityManager::goToReaderOrigin() {
  switch (APP_STATE.readerOrigin) {
    case CrossPointState::ReaderOrigin::FileBrowser:
      // The folder, not the book: leaving files an opened book into /recents, so by the
      // time the browser lists the folder the book may have moved out of it.
      goToFileBrowser(FsHelpers::extractFolderPath(APP_STATE.openEpubPath));
      return;
    case CrossPointState::ReaderOrigin::Library:
      // As the only screen, so its Back goes Home onto the Library row, as it does when
      // Home opened it.
      replaceActivity(std::make_unique<LibraryActivity>(renderer, mappedInput, APP_STATE.libraryRow));
      return;
    default:
      goHome();
      return;
  }
}

void ActivityManager::goToSleep(bool fromTimeout) {
  // An open panel eats every loop() pass, so the sleep screen would never be applied.
  if (lightPanel.isActive()) lightPanel.close([] {});
  quickLocked = false;  // Likewise, and the wake is a chip reset that would forget it anyway.
  replaceActivity(std::make_unique<SleepActivity>(renderer, mappedInput, fromTimeout));
  loop();  // Important: sleep screen must be rendered immediately, the caller will go to sleep right after this returns
}

void ActivityManager::goToBoot() { replaceActivity(std::make_unique<BootActivity>(renderer, mappedInput)); }

void ActivityManager::goToFullScreenMessage(std::string message, EpdFontFamily::Style style) {
  replaceActivity(std::make_unique<FullScreenMessageActivity>(renderer, mappedInput, std::move(message), style));
}

void ActivityManager::goHome(HomeMenuItem initialMenuItem) {
  readerMenu = MenuReturn{};
  if (initialMenuItem == HomeMenuItem::NONE && currentActivity) initialMenuItem = homeRowFor(currentActivity->name);
  replaceActivity(std::make_unique<HomeActivity>(renderer, mappedInput, initialMenuItem));
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
  // Screens close on the press or the release of Back, and some report Back as a plain
  // close, so the key itself is what says this was Back.
  poppedByBack = mappedInput.backSeenThisPass();
}

bool ActivityManager::preventAutoSleep() const { return currentActivity && currentActivity->preventAutoSleep(); }

bool ActivityManager::isReaderActivity() const {
  return std::any_of(stackActivities.begin(), stackActivities.end(),
                     [](const auto& activity) { return activity->isReaderActivity(); }) ||
         (currentActivity && currentActivity->isReaderActivity());
}

bool ActivityManager::isHomeActivity() const { return currentActivity && currentActivity->isHomeActivity(); }

bool ActivityManager::handleForcedRefresh() { return currentActivity && currentActivity->handleForcedRefresh(); }

bool ActivityManager::isBookContext() const { return currentActivity && currentActivity->isBookContext(); }

void ActivityManager::toggleQuickLock() {
  if (!quickLocked &&
      (!currentActivity || lightPanel.isActive() || pendingAction != PendingAction::None || preventAutoSleep())) {
    LOG_INF("ACT", "Quick Lock refused");
    return;
  }
  quickLocked = !quickLocked;
  LOG_INF("ACT", "Quick Lock %s", quickLocked ? "on" : "off");
  // Whatever is held now belongs to the gesture that fired this, not to the screen.
  mappedInput.suppressHeldButtonRelease();
  mappedInput.spendTouchContact();
  if (!quickLocked) currentActivity->onRevealed();
  requestUpdate(/*immediate=*/true);
}

// Over the page already in the framebuffer, like the light panel: nothing on screen is
// changing, so the screen does not render again until the lock ends.
void ActivityManager::paintQuickLockBadge() {
  constexpr int padding = 6;
  const char* text = tr(STR_LOCKED);
  int top, right, bottom, left;
  renderer.getOrientedViewableTRBL(&top, &right, &bottom, &left);
  const int width = renderer.getTextWidth(UI_10_FONT_ID, text) + padding * 2;
  const int height = renderer.getLineHeight(UI_10_FONT_ID) + padding;
  const int x = renderer.getScreenWidth() - right - width;
  renderer.fillRect(x, top, width, height);
  renderer.drawText(UI_10_FONT_ID, x + padding, top + padding / 2, text, /*black=*/false);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}

bool ActivityManager::runBoundAction(const uint8_t function) {
  // The screen on top gets first refusal: in a book, most of these actions are its own.
  if (currentActivity && currentActivity->runBoundAction(function)) {
    return true;
  }

  switch (function) {
    case CrossPointSettings::LP_MENU_GO_HOME:
      if (currentActivity && currentActivity->isHomeActivity()) return false;
      goHome();
      return true;
    case CrossPointSettings::LP_MENU_BACK:
      // A bound Back on the page is the reader's short Back.
      if (stackActivities.empty() && currentActivity && currentActivity->isReaderActivity()) {
        goToReaderOrigin();
        return true;
      }
      popActivity();
      return true;
    case CrossPointSettings::LP_MENU_SLEEP:
      if (!onSleep_) return false;
      onSleep_();
      return true;
    case CrossPointSettings::LP_MENU_FORCE_REFRESH:
      // Here rather than in main.cpp's key router, so the Confirm long-press and the reader
      // menu hold reach it too.
      LOG_DBG("ACT", "Manual screen refresh triggered");
      if (!handleForcedRefresh()) {
        RenderLock lock;
        renderer.displayBuffer(HalDisplay::HALF_REFRESH);
      }
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
      // The screen itself, not Home with the Settings row picked: goHome only moves the
      // selection, which left the button looking like it had done nothing. Over the screen
      // the panel opened on, so Back lands there and brings the panel back (the panel runs
      // its tiles in a MenuReturn::Scope).
      if (!currentActivity || currentActivity->name == activity_name::kSettings) {
        goToSettings();
        return true;
      }
      // A bound key in a book: a C3 cannot keep the reader under Settings, so Settings takes
      // its place and Back opens the book again.
      if (isReaderActivity()) {
        // No path: the reader files the book into /recents as it closes, so the reopen reads
        // APP_STATE.openEpubPath, which the filing repoints.
        replaceFromOpener(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput), Opener::Reader);
        return true;
      }
      currentActivity->startActivityForResult(makeUniqueNoThrow<SettingsActivity>(renderer, mappedInput),
                                              [](const ActivityResult&) {});
      return true;
    case CrossPointSettings::LP_MENU_LIGHT_PANEL:
      // The key that opened it puts it away again, as a second swipe does.
      if (lightPanel.isActive()) {
        lightPanel.close([this] {
          if (currentActivity) currentActivity->onRevealed();
          requestUpdate(/*immediate=*/true);
        });
        return true;
      }
      lightPanel.show();
      // Immediate for the same reason the gesture path is: ActivityManager::loop() will
      // hand the very next pass to the panel and return before its own flush.
      requestUpdate(/*immediate=*/true);
      return true;
    case CrossPointSettings::LP_MENU_TOGGLE_FRONTLIGHT: {
      if (!Frontlight.present() || lightPanel.isActive()) return false;
      SETTINGS.frontlightOn = Frontlight.isOn() ? 0 : 1;
      applyFrontlightSetting(&CrossPointSettings::frontlightOn);
      SETTINGS.saveToFile();
      return true;
    }
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
    // An immediate pass renders the state as it is by then, so it serves a deferred request
    // already pending too. Left set, the deferred one painted the same screen again on the
    // next loop: a whole second refresh after every light panel action.
    requestedUpdate = false;
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
  // If the render task currently holds renderingMutex, it is mid-render of an OLD frame.
  // We must wait for the next pass to complete.
  bool renderTaskBusy = (mutexHolder == renderTaskHandle);
  uint32_t targetPass = renderPassCompleted + (renderTaskBusy ? 2 : 1);
  if (!alreadyWaiting && !isRenderTask && !holdingRenderLock) {
    waitingTaskHandle = currTaskHandler;
    waitingTargetPass = targetPass;
  }
  taskEXIT_CRITICAL(&activityManagerSpinlock);

  // Render task cannot call requestUpdateAndWait() or it will cause a deadlock
  assert(!isRenderTask && "Render task cannot call requestUpdateAndWait()");

  // There should never be the case where 2 tasks are waiting for a render at the same time
  assert(!alreadyWaiting && "Already waiting for a render to complete");

  // Cannot call while holding RenderLock or it will cause a deadlock
  assert(!holdingRenderLock && "Cannot call requestUpdateAndWait() while holding RenderLock");

  xTaskNotify(renderTaskHandle, 1, eIncrement);
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    taskENTER_CRITICAL(&activityManagerSpinlock);
    bool done = (renderPassCompleted >= targetPass);
    if (!done) {
      waitingTaskHandle = currTaskHandler;
      waitingTargetPass = targetPass;
    }
    taskEXIT_CRITICAL(&activityManagerSpinlock);
    if (done) break;
  }
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

RenderLock::RenderLock(TryOnly) { isLocked = xSemaphoreTake(activityManager.renderingMutex, 0) == pdTRUE; }

bool RenderLock::heldByCurrentTask() {
  return xSemaphoreGetMutexHolder(activityManager.renderingMutex) == xTaskGetCurrentTaskHandle();
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
