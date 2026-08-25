#pragma once

#include <cstdint>

#include "util/BoundMenuActions.h"

// Which bound actions mean anything when no book is open.
//
// The per-button bindings offer one action list in both contexts, so the same rows are
// shown everywhere and the user does not have to learn two menus. Most actions act on
// the book being read (add a bookmark, look a word up, jump to a chapter) and have no
// target on the home screen or in settings; those are drawn greyed there and refuse to
// bind, rather than binding to a button that would silently do nothing.
namespace bound_action {

// True when `function` can run outside a book. Disabled counts as allowed: an empty
// binding is not an action, and greying it would make every unbound row look broken.
inline bool allowedOutsideBook(const uint8_t function) {
  switch (function) {
    case LP_MENU_DISABLED:
    case LP_MENU_PAGE_PREV:
    case LP_MENU_PAGE_NEXT:
    case LP_MENU_GO_HOME:
    case LP_MENU_BACK:
    case LP_MENU_LIGHT_PANEL:
    case LP_MENU_SLEEP:
      return true;
    default:
      return false;
  }
}

}  // namespace bound_action
