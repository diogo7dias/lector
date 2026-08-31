#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "UiFont.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

void LanguageSelectActivity::onEnter() {
  UiListActivity::onEnter();

  // Start on the active language rather than the top of the list.
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, currentLang);
  moveSelectionTo(it != end ? static_cast<int>(std::distance(begin, it)) : 0);
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

int LanguageSelectActivity::listFontId() const { return UBUNTU_10_FONT_ID; }

void LanguageSelectActivity::buildScreen(UiScreen& screen) {
  // The base paints the header and the button hints itself, outside the app.
  // Built on the render task, one screen at a time; the label pointers are
  // I18n statics, so the array only holds borrowed strings.
  static fui::ListItem items[totalItems];
  const auto currentLang = static_cast<uint8_t>(I18N.getLanguage());
  for (int i = 0; i < totalItems; ++i) {
    const uint8_t lang = SORTED_LANGUAGE_INDICES[i];
    items[i] = fui::ListItem{};
    items[i].label = I18N.getLanguageName(static_cast<Language>(lang));
    items[i].value = lang == currentLang ? tr(STR_SELECTED) : nullptr;
    items[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = items;
  props.count = static_cast<uint16_t>(totalItems);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void LanguageSelectActivity::activateIndex(const int index) {
  const uint8_t langIndex = SORTED_LANGUAGE_INDICES[index];

  {
    RenderLock lock(*this);
    I18N.setLanguage(static_cast<Language>(langIndex));
    // Rebind the UI font for the new language (Cozette, or Ubuntu for Arabic/Hebrew)
    // so the menus repaint in the right script without a reboot.
    bindUiFontsForLanguage(renderer);
  }

  SETTINGS.language = langIndex;
  SETTINGS.saveToFile();

  app.clearTapFlash();
  finish();
}
