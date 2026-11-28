#pragma once

#include <string>

#include "ReaderPrefs.h"

class GfxRenderer;

// Shared per-book ReaderPrefs sidecar helpers used across reader activities (EPUB,
// Markdown, and TXT) so override writes, live-edit persistence, missing-SD-font
// fallback, preset adoption, and reset-to-global cannot drift apart.
namespace book_reader_prefs {

bool writeOverride(const char* logTag, const std::string& cachePath, const ReaderPrefs& prefs);

void persistLiveEdit(const char* logTag, const std::string& cachePath, const ReaderPrefs& live,
                     const ReaderPrefs& saved, bool custom);

// If `prefs` names an SD font family no longer installed, falls back to built-in ChareInk
// and returns true.
bool fallbackMissingSdFont(const char* logTag, ReaderPrefs& prefs);

// Commits a finished TextSettings edit onto `prefs`/`custom` (preserving book-only fields)
// and writes the sidecar. Returns true when prefs changed. `persist` false keeps the change
// in memory only (the EPUB reader's Sortes mode never writes book state).
bool commitEdit(const char* logTag, const std::string& cachePath, const ReaderPrefs& edited, ReaderPrefs& prefs,
                bool& custom, bool persist = true);

// Adopts a whole ReaderPrefs (from a Reading Theme preset or Steal Look) onto
// `prefs`/`custom`, falling back missing SD fonts and writing the sidecar.
// `adoptedOut`, if non-null, receives the adopted prefs (possibly font-repaired).
// Returns true when prefs changed. `persist` as in commitEdit.
bool applyIncoming(const char* logTag, const std::string& cachePath, const ReaderPrefs& incoming, ReaderPrefs& prefs,
                   bool& custom, ReaderPrefs* adoptedOut = nullptr, bool persist = true);

// Removes the book's override sidecar and restores global defaults.
void resetToGlobal(const std::string& cachePath, ReaderPrefs& prefs, bool& custom);

}  // namespace book_reader_prefs
