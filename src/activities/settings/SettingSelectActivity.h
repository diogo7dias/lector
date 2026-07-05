#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Generic vertical list picker for a multi-option (ENUM) setting.
 *
 * Shows every option label on its own row; the user scrolls up/down and presses
 * Confirm to select. Back cancels with no change. On select it invokes
 * onSelect(index) then finishes — the caller's result handler persists and
 * rebuilds. Single-press-commit, mirroring LanguageSelectActivity, but
 * parameterised so any enum setting can reuse it instead of tap-to-cycle.
 */
class SettingSelectActivity final : public Activity {
 public:
  SettingSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string title,
                        std::vector<std::string> options, int currentIndex, std::function<void(uint8_t)> onSelect)
      : Activity("SettingSelect", renderer, mappedInput),
        title_(std::move(title)),
        options_(std::move(options)),
        selectedIndex_(currentIndex),
        originalIndex_(currentIndex),
        onSelect_(std::move(onSelect)) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();
  void onBack() { finish(); }

  std::string title_;
  std::vector<std::string> options_;
  int selectedIndex_ = 0;
  int originalIndex_ = 0;
  std::function<void(uint8_t)> onSelect_;
  ButtonNavigator buttonNavigator_;
};
