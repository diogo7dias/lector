#pragma once

#include <I18n.h>

#include <cstdint>

#include "CrossPointSettings.h"

// The label for one CrossPointSettings::LONG_PRESS_MENU_FUNCTION value.
//
// Single source of truth for all four places these actions are named: the three binding
// settings rows, the Pop-up Items tick screen and the reader's Quick Menu. Keyed on the
// enum value rather than on list position, because the tick screen skips Disabled and Menu
// Pop-up while the settings rows list every value — the two orderings cannot be shared, but
// the labels must be.
inline StrId boundMenuActionLabel(const uint8_t function) {
  switch (function) {
    case CrossPointSettings::LP_MENU_KOSYNC:
      return StrId::STR_KOSYNC;
    case CrossPointSettings::LP_MENU_DISABLED:
      return StrId::STR_DISABLED;
    case CrossPointSettings::LP_MENU_BOOKMARK:
      return StrId::STR_BOOKMARK_OPTION;
    case CrossPointSettings::LP_MENU_DICTIONARY:
      return StrId::STR_DICTIONARY;
    case CrossPointSettings::LP_MENU_GRAB_QUOTE:
      return StrId::STR_GRAB_QUOTE;
    case CrossPointSettings::LP_MENU_SELECT_CHAPTER:
      return StrId::STR_SELECT_CHAPTER;
    case CrossPointSettings::LP_MENU_GO_TO_PERCENT:
      return StrId::STR_GO_TO_PERCENT;
    case CrossPointSettings::LP_MENU_GO_TO_PARAGRAPH:
      return StrId::STR_GO_TO_PARAGRAPH;
    case CrossPointSettings::LP_MENU_FOOTNOTES:
      return StrId::STR_FOOTNOTES;
    case CrossPointSettings::LP_MENU_TEXT_SETTINGS:
      return StrId::STR_TEXT_SETTINGS;
    case CrossPointSettings::LP_MENU_READER_SETTINGS:
      return StrId::STR_READER_SETTINGS;
    case CrossPointSettings::LP_MENU_TOGGLE_STATUS_BAR:
      return StrId::STR_STATUS_BAR;
    case CrossPointSettings::LP_MENU_BOOKMARKS:
      return StrId::STR_BOOKMARKS;
    case CrossPointSettings::LP_MENU_VIEW_QUOTES:
      return StrId::STR_VIEW_QUOTES;
    case CrossPointSettings::LP_MENU_WALLPAPER_HOLD:
      // Reuses the retired Display row's label rather than the in-book menu's, which
      // flips between "Hold this wallpaper" and "Resume rotation" with the current state.
      // A binding names the action, not what it will do next time.
      return StrId::STR_PAUSE_WALLPAPER_ROTATION;
    case CrossPointSettings::LP_MENU_NEARBY_SEND_BOOK:
      return StrId::STR_NEARBY_SEND_FILE;
    case CrossPointSettings::LP_MENU_PAGE_PREV:
      return StrId::STR_PREV_PAGE;
    case CrossPointSettings::LP_MENU_PAGE_NEXT:
      return StrId::STR_NEXT_PAGE;
    case CrossPointSettings::LP_MENU_GO_HOME:
      return StrId::STR_HOME;
    case CrossPointSettings::LP_MENU_BACK:
      return StrId::STR_BACK;
    case CrossPointSettings::LP_MENU_LIGHT_PANEL:
      return StrId::STR_FRONTLIGHT;
    case CrossPointSettings::LP_MENU_SLEEP:
      return StrId::STR_SLEEP;
    case CrossPointSettings::LP_MENU_POPUP:
    default:
      return StrId::STR_MENU_POPUP;
  }
}
