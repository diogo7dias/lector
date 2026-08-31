#pragma once

#include <I18nKeys.h>

#include <string_view>

// Which label the busy strip carries while a screen change is being built.
//
// Keyed on Activity::name because that is the only thing the manager knows about
// a screen it has not entered yet, and because a screen that is slow for its own
// reasons still installs its own nested banner underneath this one.
//
// Pure and header-only so the mapping is testable on the host: the strip itself
// needs a panel, the choice of words does not.
namespace activity_busy {

inline StrId labelFor(const std::string_view name) {
  if (name == "Home") return StrId::STR_BUSY_GOING_HOME;
  if (name == "FileBrowser") return StrId::STR_BUSY_READING_FOLDER;
  if (name == "Settings") return StrId::STR_BUSY_LOADING_SETTINGS;
  if (name == "QuotesViewer") return StrId::STR_BUSY_LOADING_QUOTES;
  if (name == "InstalledFonts" || name == "FontPicker" || name == "FontDownload") {
    return StrId::STR_BUSY_LOADING_FONTS;
  }
  // Every reader shares one label: the user picked a book, not a format.
  if (name == "Reader" || name == "EpubReader" || name == "TxtReader" || name == "XtcReader") {
    return StrId::STR_BUSY_OPENING_BOOK;
  }
  // Anything that has to reach the network before it can draw. These are the waits
  // that run into seconds, so naming them is worth more than one shared word.
  if (name == "CrossPointWebServer" || name == "OpdsBookBrowser" || name == "OpdsServerList" ||
      name == "WifiSelection" || name == "CalibreConnect" || name == "OtaUpdate" || name == "NearbyFileTransfer" ||
      name == "NearbyPositionSync" || name == "KOReaderSync" || name == "KOReaderAuth") {
    return StrId::STR_BUSY_CONNECTING;
  }
  return StrId::STR_BUSY_OPENING;
}

// Screens that must never be covered by the strip. Sleep paints three grayscale
// plane passes and then the device powers down, so a banner would either be the
// last thing left on the panel or waste the one refresh budget it has; Boot is
// the same picture on the way in.
inline bool wantsBanner(const std::string_view name) { return name != "Sleep" && name != "Boot"; }

}  // namespace activity_busy
