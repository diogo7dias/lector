#pragma once

#include <I18n.h>

#include "activities/UiListActivity.h"

class GfxRenderer;
class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public UiListActivity {
 public:
  LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("LanguageSelect", renderer, mappedInput) {}

  void onEnter() override;

 protected:
  int listCount() const override { return totalItems; }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  const char* headerTitle() const override;
  // Native language names span Arabic, Hebrew, Cyrillic and Latin, so the rows
  // MUST use the full-coverage Ubuntu font whatever the active UI font is.
  int listFontId() const override;

 private:
  constexpr static int totalItems = getLanguageCount();
};
