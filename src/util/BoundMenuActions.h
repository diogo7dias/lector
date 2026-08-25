#pragma once

#include <cstdint>

// The actions a button, a gesture or a pop-up row can be bound to.
//
// Lives apart from CrossPointSettings so the pure rules about these values (which of
// them work outside a book, what each is called) can be built and tested without the
// storage layer and its Arduino dependencies. CrossPointSettings aliases every name
// below, so CrossPointSettings::LP_MENU_* keeps working and keeps meaning exactly this.
namespace bound_action {

// Long-press Confirm action while reading an EPUB. The setting cycles through these values.
// Persisted in settings.json by index: any new function (e.g. dictionary, bookmark) MUST use a
// value >= 2 and be appended at the END of the enumValues array in SettingsList.h, otherwise the
// stored indices shift and existing saves are silently misinterpreted.
enum LONG_PRESS_MENU_FUNCTION {
  LP_MENU_KOSYNC = 0,
  LP_MENU_DISABLED = 1,
  LP_MENU_BOOKMARK = 2,
  LP_MENU_DICTIONARY = 3,
  LP_MENU_GRAB_QUOTE = 4,
  // Appended for the shared binding list (0.20). Order is frozen: the values
  // below double as bit positions in popupItems, so reordering them would both
  // shift saved bindings and silently re-tick a different set of pop-up rows.
  // RETIRED 2026-08-11 (Diogo): both left the offered list. The values stay so every
  // binding value after them keeps its meaning, they are listed in withHiddenEnumValues()
  // in SettingsList.h, and fromJson folds a binding still set to one of them to Disabled.
  // LP_MENU_TEXT_SETTINGS below was retired the same way on 2026-08-22: the global text
  // settings belong in Settings > Reader, and having them a button press from the page
  // made it easy to edit every book while meaning to edit this one (Reader Settings).
  LP_MENU_SELECT_CHAPTER = 5,
  LP_MENU_GO_TO_PERCENT = 6,
  LP_MENU_GO_TO_PARAGRAPH = 7,
  LP_MENU_FOOTNOTES = 8,
  LP_MENU_TEXT_SETTINGS = 9,
  LP_MENU_READER_SETTINGS = 10,
  LP_MENU_TOGGLE_STATUS_BAR = 11,
  // Not an action: opens the pop-up built from popupItems. Always last, and it is
  // the only value runBoundMenuFunction() refuses to run, so a pop-up cannot list
  // itself and recurse.
  LP_MENU_POPUP = 12,
  // Holds the wallpaper the lock screen last showed, instead of picking a new one at
  // the next sleep. Appended after LP_MENU_POPUP, so Menu Pop-up is no longer the last
  // value even though it is still the only non-action one.
  LP_MENU_WALLPAPER_HOLD = 13,
  // The list of saved bookmarks, next to the toggle that adds one.
  LP_MENU_BOOKMARKS = 14,
  // The saved-quotes viewer, next to Grab Quote that writes to it.
  LP_MENU_VIEW_QUOTES = 15,
  // Sends this book's own file to another reader over ESP-NOW. Bindable to a
  // button, but deliberately absent from POPUP_ITEM_FUNCTIONS below: popupItems
  // is a 16-bit mask keyed by these values, and bit 15 is already the last one
  // there is. Listing it in the pop-up means widening that mask and migrating
  // every stored value.
  LP_MENU_NEARBY_SEND_BOOK = 16,
  // Appended for the per-button bindings (Buttons settings screen). The first four
  // are what a button does rather than what a menu offers, so they are absent from
  // POPUP_ITEM_FUNCTIONS: ticking "Next page" into the reader's pop-up would be a
  // row that turns the page the pop-up is covering.
  LP_MENU_PAGE_PREV = 17,
  LP_MENU_PAGE_NEXT = 18,
  LP_MENU_GO_HOME = 19,
  LP_MENU_BACK = 20,
  LP_MENU_LIGHT_PANEL = 21,
  LP_MENU_SLEEP = 22,
  // Appended when the power button joined the Buttons screen: its old shortPwrBtn
  // setting could force a screen refresh, and folding that setting into the shared
  // bindings would have dropped the ability if the action did not come with it.
  LP_MENU_FORCE_REFRESH = 23,
  LONG_PRESS_MENU_FUNCTION_COUNT
};

}  // namespace bound_action
