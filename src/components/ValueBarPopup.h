#pragma once
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

// In-place modal bar editor for a big-value numeric setting, drawn over whatever is
// behind it (e.g. the reader-settings live preview stays put). Mirrors OptionPopup's
// show / handleInput / processRender / isActive shape so activities drive it the same way.
//
// Button model (restored from old lector's IntervalSelectionActivity): the two front
// Left/Right buttons step by smallStep (±1); the side Up/Down buttons step by largeStep
// (±5). On X3 the side buttons sit on the physical left/right edges, so the large-step
// direction is flipped there so left decreases and right increases. Confirm commits,
// Back cancels. Both steps auto-repeat while held.
class ValueBarPopup {
 public:
  void show(StrId titleId, int minValue, int maxValue, int smallStep, int largeStep, int current, StrId stepHintId,
            std::function<void(int)> onCommit) {
    title = I18N.get(titleId);
    minValue_ = minValue;
    maxValue_ = maxValue;
    smallStep_ = smallStep;
    largeStep_ = largeStep;
    stepHintId_ = stepHintId;
    value_ = clamp(current);
    onCommitCallback = std::move(onCommit);
    active = true;
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    if (input.wasPressed(MappedInputManager::Button::Back)) {
      active = false;  // cancel: leave the setting unchanged
      requestUpdate();
      return true;
    }
    if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      active = false;
      if (onCommitCallback) onCommitCallback(value_);
      requestUpdate();
      return true;
    }

    // The nav callbacks fire synchronously inside these calls, so capturing
    // requestUpdate by reference is safe (never stored past this handleInput).
    nav_.onPressAndContinuous({MappedInputManager::Button::Left},
                              [this, &requestUpdate] { adjustBy(-smallStep_, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Right},
                              [this, &requestUpdate] { adjustBy(smallStep_, requestUpdate); });
    const int upDelta = gpio.deviceIsX3() ? -largeStep_ : largeStep_;
    const int downDelta = gpio.deviceIsX3() ? largeStep_ : -largeStep_;
    nav_.onPressAndContinuous({MappedInputManager::Button::Up},
                              [this, upDelta, &requestUpdate] { adjustBy(upDelta, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down},
                              [this, downDelta, &requestUpdate] { adjustBy(downDelta, requestUpdate); });
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    render(renderer);
    const auto labels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), "-", "+");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    const auto& m = UITheme::getInstance().getMetrics();
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();

    const int titleLH = renderer.getLineHeight(UI_12_FONT_ID);
    const int valueLH = renderer.getLineHeight(UI_12_FONT_ID);
    const int hintLH = renderer.getLineHeight(SMALL_FONT_ID);
    constexpr int barH = 16;
    const int gap = m.optionPopupTitleGap;
    const int innerPad = m.optionPopupInnerPadding;

    const int contentH = titleLH + gap + valueLH + gap + barH + gap + hintLH;
    // Full width, black backing, white inset border — the same surface every other
    // popup uses, so the firmware has one popup look and not three.
    const int dialogW = screenW;
    const int dialogH = contentH + innerPad * 2;
    const int dialogX = 0;
    const int dialogY = (screenH - dialogH) / 2;

    const int ft = m.popupFrameThickness;
    renderer.fillRect(dialogX, dialogY, dialogW, dialogH, true);
    renderer.drawRect(dialogX, dialogY, dialogW, dialogH, ft, false);

    int y = dialogY + innerPad;
    renderer.drawCenteredText(UI_12_FONT_ID, y, title.c_str(), false, EpdFontFamily::REGULAR);
    y += titleLH + gap;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value_);
    renderer.drawCenteredText(UI_12_FONT_ID, y, buf, false, EpdFontFamily::REGULAR);
    y += valueLH + gap;

    // Bar drawn in white on the black panel: outline, fill and knob all inverted.
    const int barWidth = std::min(dialogW - innerPad * 2, 360);
    const int barX = dialogX + (dialogW - barWidth) / 2;
    const int barY = y;
    renderer.drawRect(barX, barY, barWidth, barH, false);
    const int range = std::max(1, maxValue_ - minValue_);
    const int fillWidth = (barWidth - 4) * (value_ - minValue_) / range;
    if (fillWidth > 0) {
      renderer.fillRect(barX + 2, barY + 2, fillWidth, barH - 4, false);
    }
    const int knobX = std::max(barX + 2, barX + 2 + fillWidth - 2);
    renderer.fillRect(knobX, barY - 4, 4, barH + 8, false);
    y += barH + gap;

    renderer.drawCenteredText(SMALL_FONT_ID, y, I18N.get(stepHintId_), false);
  }

  bool isActive() const { return active; }

 private:
  int clamp(int v) const { return std::clamp(v, minValue_, maxValue_); }

  void adjustBy(int delta, const std::function<void()>& requestUpdate) {
    const int next = clamp(value_ + delta);
    if (next != value_) {
      value_ = next;
      requestUpdate();
    }
  }

  bool active = false;
  std::string title;
  StrId stepHintId_ = StrId::STR_NONE_OPT;
  int value_ = 0;
  int minValue_ = 0;
  int maxValue_ = 0;
  int smallStep_ = 1;
  int largeStep_ = 5;
  std::function<void(int)> onCommitCallback;
  // Faster auto-repeat than the default nav cadence so a wide range is quick to cross.
  ButtonNavigator nav_{120, 350};
};
