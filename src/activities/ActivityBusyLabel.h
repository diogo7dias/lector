#pragma once

#include <I18nKeys.h>

#include <string_view>

#include "ActivityNames.h"

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
  if (name == activity_name::kHome) return StrId::STR_BUSY_GOING_HOME;
  if (name == activity_name::kFileBrowser) return StrId::STR_BUSY_READING_FOLDER;
  if (name == activity_name::kSettings) return StrId::STR_BUSY_LOADING_SETTINGS;
  if (name == activity_name::kQuotesViewer) return StrId::STR_BUSY_LOADING_QUOTES;
  if (name == activity_name::kInstalledFonts || name == activity_name::kFontPicker ||
      name == activity_name::kFontDownload) {
    return StrId::STR_BUSY_LOADING_FONTS;
  }
  // Every reader shares one label: the user picked a book, not a format.
  if (name == activity_name::kReader || name == activity_name::kEpubReader || name == activity_name::kTxtReader ||
      name == activity_name::kXtcReader) {
    return StrId::STR_BUSY_OPENING_BOOK;
  }
  // Anything that has to reach the network before it can draw. These are the waits
  // that run into seconds, so naming them is worth more than one shared word.
  if (name == activity_name::kCrossPointWebServer || name == activity_name::kOpdsBookBrowser ||
      name == activity_name::kOpdsServerList || name == activity_name::kWifiSelection ||
      name == activity_name::kCalibreConnect || name == activity_name::kOtaUpdate ||
      name == activity_name::kNearbyFileTransfer || name == activity_name::kNearbyPositionSync ||
      name == activity_name::kKOReaderSync || name == activity_name::kKOReaderAuth) {
    return StrId::STR_BUSY_CONNECTING;
  }
  return StrId::STR_BUSY_OPENING;
}

// Screens that must never be covered by the strip. Sleep paints three grayscale
// plane passes and then the device powers down, so a banner would either be the
// last thing left on the panel or waste the one refresh budget it has; Boot is
// the same picture on the way in.
inline bool wantsBanner(const std::string_view name) {
  return name != activity_name::kSleep && name != activity_name::kBoot;
}

}  // namespace activity_busy
