#include "BookReaderPrefs.h"

#include <HalStorage.h>
#include <Logging.h>

#include <algorithm>
#include <cstring>

#include "CrossPointSettings.h"
#include "SdCardFontSystem.h"

namespace book_reader_prefs {

bool writeOverride(const char* const logTag, const std::string& cachePath, const ReaderPrefs& prefs) {
  HalFile f;
  if (!Storage.openFileForWrite(logTag, readerSidecarPath(cachePath), f)) {
    LOG_ERR(logTag, "Failed to open reader_override.bin for write");
    return false;
  }
  if (!writeReaderPrefs(f, prefs)) {
    LOG_ERR(logTag, "Short write to reader_override.bin");
    return false;
  }
  return true;
}

void persistLiveEdit(const char* const logTag, const std::string& cachePath, const ReaderPrefs& live,
                     const ReaderPrefs& saved, const bool custom) {
  const ReaderOverrideDecision decision = decideReaderOverride(live, saved, custom);
  switch (decision.action) {
    case ReaderOverrideAction::Write:
      writeOverride(logTag, cachePath, decision.prefs);
      break;
    case ReaderOverrideAction::Remove:
      Storage.remove(readerSidecarPath(cachePath).c_str());
      break;
    case ReaderOverrideAction::Keep:
      break;
  }
}

bool fallbackMissingSdFont(const char* const logTag, ReaderPrefs& prefs) {
  if (prefs.sdFontFamilyName[0] == '\0') return false;
  const auto& families = sdFontSystem.registry().getFamilies();
  const bool installed = std::any_of(families.begin(), families.end(), [&](const SdCardFontFamilyInfo& fam) {
    return fam.name == prefs.sdFontFamilyName;
  });
  if (installed) return false;
  LOG_ERR(logTag, "Reader prefs name a missing SD font '%s' — falling back to the built-in family",
          prefs.sdFontFamilyName);
  std::memset(prefs.sdFontFamilyName, 0, sizeof(prefs.sdFontFamilyName));
  prefs.fontFamily = CrossPointSettings::CHAREINK;
  return true;
}

bool commitEdit(const char* const logTag, const std::string& cachePath, const ReaderPrefs& edited, ReaderPrefs& prefs,
                bool& custom) {
  ReaderPrefs next = edited;
  restoreBookOnlyFields(next, prefs);
  if (std::memcmp(&next, &prefs, sizeof(ReaderPrefs)) == 0) return false;
  prefs = next;
  custom = true;
  writeOverride(logTag, cachePath, prefs);
  return true;
}

bool applyIncoming(const char* const logTag, const std::string& cachePath, const ReaderPrefs& incoming,
                   ReaderPrefs& prefs, bool& custom, ReaderPrefs* const adoptedOut) {
  ReaderPrefs next = incoming;
  fallbackMissingSdFont(logTag, next);
  if (adoptedOut) *adoptedOut = next;
  if (std::memcmp(&next, &prefs, sizeof(ReaderPrefs)) == 0) return false;
  prefs = next;
  custom = true;
  writeOverride(logTag, cachePath, prefs);
  return true;
}

void resetToGlobal(const std::string& cachePath, ReaderPrefs& prefs, bool& custom) {
  Storage.remove(readerSidecarPath(cachePath).c_str());
  prefs = ReaderPrefs::fromGlobal();
  custom = false;
}

}  // namespace book_reader_prefs
