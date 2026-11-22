// The summary line on each Settings hub card: two live values of that category, so the
// hub says how the reader is set up rather than how many rows a category holds.
#include <HalGPIO.h>
#include <I18n.h>

#include <cstdio>
#include <string>

#include "CrossPointSettings.h"
#include "SettingsActivity.h"

namespace {

// One "label value" pair from a translated format with a single %s.
std::string pair(const StrId formatId, const std::string& value) {
  if (value.empty()) return {};
  char text[96];
  snprintf(text, sizeof(text), I18N.get(formatId), value.c_str());
  return text;
}

std::string joined(const std::string& first, const std::string& second) {
  if (first.empty()) return second;
  if (second.empty()) return first;
  return first + " \xC2\xB7 " + second;  // U+00B7 middle dot, in Cozette and Ubuntu
}

}  // namespace

std::string SettingsActivity::hubSummary(const int index) const {
  // A row's value as the category shows it, or empty when this board does not list it.
  const auto valueOf = [this](const std::vector<SettingInfo>& rows, uint8_t CrossPointSettings::* member) {
    for (const SettingInfo& row : rows) {
      if (!row.isHeader && row.valuePtr == member) return settingValueText(row);
    }
    return std::string();
  };
  switch (index) {
    case 0:
      return joined(pair(StrId::STR_SUMMARY_REFRESH, valueOf(displaySettings, &CrossPointSettings::refreshFrequency)),
                    pair(StrId::STR_SUMMARY_SLEEP_SCREEN, valueOf(displaySettings, &CrossPointSettings::sleepScreen)));
    case 1: {
      const char* family = SETTINGS.sdFontFamilyName[0] != '\0' ? SETTINGS.sdFontFamilyName : tr(STR_CHAREINK);
      char font[48];
      snprintf(font, sizeof(font), "%s %u", family, static_cast<unsigned>(SETTINGS.fontPointSize));
      return joined(font, pair(StrId::STR_SUMMARY_RULED, valueOf(readerSettings, &CrossPointSettings::ruledLines)));
    }
    case 2: {
      const std::string sideKeys =
          pair(StrId::STR_SUMMARY_SIDE_KEYS, valueOf(controlsSettings, &CrossPointSettings::sideButtonLayout));
      if (gpio.hasTouch()) {
        return joined(
            pair(StrId::STR_SUMMARY_TOUCH, valueOf(controlsSettings, &CrossPointSettings::touchReaderControls)),
            sideKeys);
      }
      return joined(sideKeys, pair(StrId::STR_SUMMARY_HOLD,
                                   valueOf(controlsSettings, &CrossPointSettings::longPressMenuFunction)));
    }
    case 3:
      return joined(
          pair(StrId::STR_SUMMARY_AUTO_SLEEP, valueOf(systemSettings, &CrossPointSettings::sleepTimeoutMinutes)),
          I18N.getLanguageName(I18N.getLanguage()));
    default:
      return tr(STR_SUMMARY_FILE_TRANSFER);
  }
}
