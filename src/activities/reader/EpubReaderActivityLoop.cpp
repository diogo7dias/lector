#include <HalStorage.h>
#include <Logging.h>

#include "CrossPointSettings.h"
#include "DictionaryHistoryActivity.h"
#include "DictionaryWordSelectActivity.h"
#include "EpubReaderActivity.h"
#include "EpubReaderBookmarksActivity.h"
#include "EpubReaderChapterSelectionActivity.h"
#include "EpubReaderFootnotesActivity.h"
#include "EpubReaderSearchActivity.h"
#include "KOReaderAutoSync.h"
#include "KOReaderSyncActivity.h"
#include "MappedInputManager.h"
#include "NearbyPositionSyncActivity.h"
#include "QuoteSelectActivity.h"
#include "QuotesViewerActivity.h"
#include "ReaderMargins.h"
#include "ReaderPresetStore.h"
#include "ReaderPresetsActivity.h"
#include "ReaderUtils.h"
#include "RecentBooksStore.h"
#include "StealLookActivity.h"
#include "activities/network/NearbyFileTransferActivity.h"
#include "activities/settings/StatusBarSettingsActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "util/BookFiling.h"
#include "util/SortesSelection.h"

namespace {
bool isInReadFolder(const std::string& path) { return bookfiling::isInFolder(path, bookfiling::READ_FOLDER); }
}  // namespace

void EpubReaderActivity::loop() {
  if (!epub) {
    // Should never happen
    finish();
    return;
  }

  rememberBookOnceRendered();
  // Auto Sync pulls once the first page is up, so the book never waits on the radio.
  if (bookRemembered && !autoPullChecked && !sortesMode) {
    autoPullChecked = true;
    if (ko_auto_sync::claimPull(epub->getPath()) && launchKOReaderSync(true)) return;
  }

  // The Quick Menu owns every button while it is up, and it MUST be offered them BEFORE
  // the latches below observe anything.
  //
  // The pop-up acts on button PRESS; the latches exist to pair a press with its release.
  // Observing first latched a press the pop-up then consumed, so on the next pass — with
  // the pop-up already closed — the trailing release reached handleBackNavigation with a
  // latched press behind it and read as a genuine Back on the page: closing the pop-up
  // sent the reader to the home screen. Confirm had the same hole, being gated by the
  // same latch, and would have reopened the reader menu.
  //
  // Handled here, a press the pop-up eats is never latched at all, and the stray-release
  // branch in handleBackNavigation swallows the trailing release exactly as it does for
  // any child screen that closes on press.
  if (quickMenu.handleInput(mappedInput, [this] { requestUpdate(); })) {
    // The pop-up painted a framed panel straight onto the page. The page repaint that
    // replaces it must be a cleanup refresh, or the frame ghosts through it.
    if (!quickMenu.isActive()) scheduleGhostCleanup();
    return;
  }

  // Must run before any early return below, so a genuine press is never missed. See
  // ReaderUtils::ButtonPressLatch: the reader acts on release, child screens close on
  // press, and an unpaired release used to throw the user out of the book.
  backLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Back));
  confirmLatch_.observe(mappedInput.wasPressed(MappedInputManager::Button::Confirm));

  runIdleBackgroundWork();

  // End-of-Book screen reached (currentSpineIndex == spine count) means the book is
  // finished. Two independent finished-book features key off this same condition.
  const bool atEndOfBook = currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount();

  // Drop this book from the Recent Books list; if the reader then pages back into the book,
  // re-add it. So removal only sticks if the reader leaves while still on the End-of-Book
  // screen. Acts only on the transition (guarded by recentsEntryRemoved) — no per-frame writes.
  if (!sortesMode && SETTINGS.removeReadBooksFromRecents) {
    if (atEndOfBook && !recentsEntryRemoved) {
      // Only treat the book as "removed by us" if it was actually in the list, so the
      // re-add branch below doesn't insert a book the feature never removed.
      recentsEntryRemoved = RECENT_BOOKS.removeByPath(epub->getPath());
    } else if (!atEndOfBook && recentsEntryRemoved) {
      // Re-add (goes to front of the list via addBook — accepted ordering side effect).
      RECENT_BOOKS.addBook(epub->getPath(), epub->getTitle(), epub->getAuthor());
      recentsEntryRemoved = false;
    }
  }

  // Arm the move here so ANY exit path (Back, Home, file browser) relocates the book into
  // /Read/ in onExit(); paging back off the end screen disarms it (book not actually
  // finished). If removeReadBooksFromRecents also fired, RecentBooksStore::updatePath in the
  // move path becomes a safe no-op since the entry was already removed.
  // Reached by turning pages this sitting, not reopened on the end screen.
  if (atEndOfBook && journal.pages > 0) journal.finished = true;
  if (atEndOfBook) {
    pendingReadFolderMove = !sortesMode && SETTINGS.moveFinishedToReadFolder && !isInReadFolder(epub->getPath());
  } else {
    pendingReadFolderMove = false;
  }

  if (showBookmarkMessage && (millis() - bookmarkMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showBookmarkMessage = false;
    // The banner sat over the page; clear its residue on the paint that removes it.
    scheduleGhostCleanup();
    requestUpdate();
  }

  if (showDictionaryMessage && (millis() - dictionaryMessageTime) >= ReaderUtils::BOOKMARK_MESSAGE_DURATION_MS) {
    showDictionaryMessage = false;
    scheduleGhostCleanup();
    requestUpdate();
  }

  // While the end screen suggestion menu is showing it owns Confirm/Back/navigation
  // input. Anything it doesn't handle (e.g. long-press Back to the file browser) falls
  // through to the regular handlers below; page turns are absorbed by the end-of-book
  // block. A Confirm release after a long-press function (bookmark/sync) fired is left
  // to the regular Confirm handler below, which consumes it via ignoreNextConfirmRelease.
  if (atEndOfBook && endOfBookOptions.menuActive() &&
      !(ignoreNextConfirmRelease && mappedInput.wasReleased(MappedInputManager::Button::Confirm))) {
    std::string openPath;
    switch (endOfBookOptions.handleMenuInput(mappedInput, &openPath)) {
      case EndOfBookOptions::Action::OpenBook:
        activityManager.goToReader(openPath);
        return;
      case EndOfBookOptions::Action::GoHome:
        onGoHome();
        return;
      case EndOfBookOptions::Action::LastPage:
        returnToLastPage();
        return;
      case EndOfBookOptions::Action::Redraw:
        requestUpdate();
        return;
      case EndOfBookOptions::Action::None:
        break;
    }
  }

  // Enter reader menu activity on Confirm or a downward swipe from the top edge.
  //
  // With no function bound to a long press, Confirm carries exactly one action, so the
  // menu opens the instant the button goes down. With one bound the two cannot be told
  // apart until the button comes up, so the menu waits for the release, and a hold that
  // already fired its function sets ignoreNextConfirmRelease to keep that release from
  // also opening the menu.

  // Holding a word looks it up. The long press fires mid-contact and suppresses the rest
  // of it, so the lift neither turns the page nor taps the picker that opens.
  // Two fingers spread or pinched step the text size once per gesture, like the size row
  // in the light panel. The contact is suppressed so the lift neither taps nor swipes.
  int pinchX1 = 0;
  int pinchY1 = 0;
  int pinchX2 = 0;
  int pinchY2 = 0;
  if (SETTINGS.touchReaderControls != CrossPointSettings::TOUCH_READER_OFF &&
      mappedInput.getTwoFingerTouch(pinchX1, pinchY1, pinchX2, pinchY2)) {
    if (!pinchGesture.isActive()) gpio.suppressTouchContact();
    const auto action = pinchGesture.update(pinchX1, pinchY1, pinchX2, pinchY2);
    if (action != ReaderPinchGesture::Action::None) {
      lightPanelStepAux(action == ReaderPinchGesture::Action::Increase ? 1 : -1);
    }
    return;
  }
  pinchGesture.reset();

  int holdX = 0;
  int holdY = 0;
  if (mappedInput.wasScreenLongPress(holdX, holdY)) {
    openDictionaryWordSelect(holdX, holdY);
    return;
  }
  // Touch on the open page, read once per loop: a tap or swipe can turn the page or
  // open the menu. Read before the Confirm handling below so the menu opens from
  // either input on the same pass.
  const auto touch = ReaderUtils::detectTouchPageTurn(renderer, mappedInput);
  if (ReaderUtils::isTouchMenuGesture(renderer, mappedInput, touch.menu)) {
    openReaderMenu();
    return;
  }

  const bool confirmHasHold = SETTINGS.longPressMenuFunction != CrossPointSettings::LP_MENU_DISABLED;
  const bool confirmEdge = confirmHasHold
                               ? confirmLatch_.release(mappedInput.wasReleased(MappedInputManager::Button::Confirm))
                               : mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  if (confirmEdge) {
    if (ignoreNextConfirmRelease) {
      ignoreNextConfirmRelease = false;
    } else {
      openReaderMenu();
    }
  }

  // Long-press Confirm runs the user-selected function (SETTINGS.longPressMenuFunction).
  //
  // confirmLatch_.seen gates this for the same reason it gates the release above: the
  // hold must have STARTED here. Popups act on the press (OptionPopup, and so every
  // ConfirmationActivity), so confirming one hands this activity a button that is still
  // held, with a held-time already past the threshold. Without this check, confirming
  // "Delete wallpaper?" from the reader menu ran the bound long-press function the moment
  // the reader resumed -- which, on the default KOSync binding, silently started WiFi and
  // a sync the user never asked for. An inherited hold is now ignored until the button is
  // released and pressed again inside the reader.
  if (confirmLatch_.seen && mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
    // KOSync asks for a longer hold (~1s) than the others (~0.4s): it starts WiFi, so an
    // accidental brush must not trigger it. A function that declines (KOSync without
    // credentials) leaves the release alone, so the normal reader menu still opens.
    const unsigned long holdThreshold = (SETTINGS.longPressMenuFunction == CrossPointSettings::LP_MENU_KOSYNC)
                                            ? ReaderUtils::GO_HOME_MS
                                            : ReaderUtils::BOOKMARK_HOLD_MS;
    if (SETTINGS.longPressMenuFunction != CrossPointSettings::LP_MENU_DISABLED &&
        mappedInput.getHeldTime() >= holdThreshold && activityManager.runBoundAction(SETTINGS.longPressMenuFunction)) {
      ignoreNextConfirmRelease = true;  // suppress the menu on the release that follows
      return;
    }
  }

  if (sortesMode && !mappedInput.wasBackGesture() &&
      backLatch_.release(mappedInput.wasReleased(MappedInputManager::Button::Back))) {
    onGoHome();
    return;
  }

  // Short press Back restores position when viewing a footnote (takes priority over navigation)
  if (footnoteDepth > 0 && mappedInput.wasReleased(MappedInputManager::Button::Back) &&
      mappedInput.getHeldTime() < ReaderUtils::GO_BACK_OR_HOME_MS) {
    restoreSavedPosition();
    return;
  }

  if (ReaderUtils::handleBackNavigation(mappedInput, activityManager, epub ? epub->getPath().c_str() : "",
                                        {this, [](void* ctx) { static_cast<EpubReaderActivity*>(ctx)->onGoHome(); }},
                                        backLatch_)) {
    return;
  }

  // auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);

  auto [prevTriggered, nextTriggered] = ReaderUtils::detectPageTurn(mappedInput);
  prevTriggered = prevTriggered || touch.prev;
  nextTriggered = nextTriggered || touch.next;
  if (!prevTriggered && !nextTriggered) {
    return;
  }

  // At end of the book with no suggestion menu, forward button goes home and back
  // button returns to last page
  if (currentSpineIndex > 0 && currentSpineIndex >= epub->getSpineItemsCount()) {
    if (endOfBookOptions.menuActive()) {
      // Selection movement was handled above; absorb leftover page-turn triggers so
      // e.g. "previous" at the top of the list doesn't jump back into the book
      return;
    }
    if (nextTriggered) {
      onGoHome();
    } else {
      returnToLastPage();
    }
    return;
  }

  // Screenshot is Power plus Down together; neither half should also turn a page.
  if (gpio.wasReleased(HalGPIO::BTN_POWER) && gpio.wasReleased(HalGPIO::BTN_DOWN)) {
    return;
  }

  // No current section, attempt to rerender the book
  if (!section) {
    requestUpdate();
    return;
  }

  if (prevTriggered) {
    pageTurn(false);
  } else {
    pageTurn(true);
  }
}
