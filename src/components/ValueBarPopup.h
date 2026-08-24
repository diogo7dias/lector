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

    // Touch drives the bar directly: press anywhere along it, or drag across it, and the
    // value follows the finger. The bar is the control, so this is what a touch user
    // reaches for first — and it does not depend on the front-button hints underneath.
    int tx = 0;
    int ty = 0;
    if (barW_ > 0 && (input.isScreenTouchHeld(tx, ty) || input.wasScreenTouchDown(tx, ty))) {
      if (ty >= barY_ - kTouchSlack && ty <= barY_ + barH_ + kTouchSlack) {
        const int next = clamp(valueForX(tx));
        if (next != value_) {
          value_ = next;
          requestUpdate();
        }
        return true;
      }
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

  void render(const GfxRenderer& renderer) const {  // NOLINT: records the bar rect for touch
    if (!active) return;
    const auto& m = UITheme::getInstance().getMetrics();
    const int screenW = renderer.getScreenWidth();
    const int screenH = renderer.getScreenHeight();

    const int titleLH = renderer.getLineHeight(UI_10_FONT_ID);
    const int valueLH = renderer.getLineHeight(UI_10_FONT_ID);
    const int hintLH = renderer.getLineHeight(SMALL_FONT_ID);
    constexpr int barH = 16;
    const int gap = m.optionPopupTitleGap;
    const int innerPad = m.optionPopupInnerPadding;

    const int contentH = titleLH + gap + valueLH + gap + barH + gap + hintLH;
    const int dialogW = std::min(400, std::max(0, screenW - m.optionPopupDialogSideMargin * 2));
    const int dialogH = contentH + innerPad * 2;
    const int dialogX = (screenW - dialogW) / 2;
    const int dialogY = (screenH - dialogH) / 2;

    // Framed panel over the background (white body, black border), mirroring drawOptionPopup.
    const int ft = m.popupFrameThickness;
    const int fr = m.popupCornerRadius;
    if (fr > 0) {
      renderer.fillRoundedRect(dialogX - ft, dialogY - ft, dialogW + ft * 2, dialogH + ft * 2, fr + ft, Color::White);
      renderer.fillRoundedRect(dialogX, dialogY, dialogW, dialogH, fr, Color::Black);
      renderer.fillRoundedRect(dialogX + ft, dialogY + ft, dialogW - ft * 2, dialogH - ft * 2,
                               fr - ft > 0 ? fr - ft : 0, Color::White);
    } else {
      renderer.fillRect(dialogX - ft, dialogY - ft, dialogW + ft * 2, dialogH + ft * 2, true);
      renderer.fillRect(dialogX, dialogY, dialogW, dialogH, false);
    }

    int y = dialogY + innerPad;
    renderer.drawCenteredText(UI_10_FONT_ID, y, title.c_str(), true, EpdFontFamily::REGULAR);
    y += titleLH + gap;

    char buf[32];
    snprintf(buf, sizeof(buf), "%d", value_);
    renderer.drawCenteredText(UI_10_FONT_ID, y, buf, true, EpdFontFamily::REGULAR);
    y += valueLH + gap;

    const int barWidth = std::min(dialogW - innerPad * 2, 360);
    const int barX = dialogX + (dialogW - barWidth) / 2;
    const int barY = y;
    // Remember where the bar landed so handleInput can hit-test it. Layout depends on the
    // theme metrics and the screen, so it is cheaper to record what was drawn than to
    // recompute it on every touch sample.
    barX_ = barX;
    barY_ = barY;
    barW_ = barWidth;
    barH_ = barH;
    renderer.drawRect(barX, barY, barWidth, barH);
    const int range = std::max(1, maxValue_ - minValue_);
    const int fillWidth = (barWidth - 4) * (value_ - minValue_) / range;
    if (fillWidth > 0) {
      renderer.fillRect(barX + 2, barY + 2, fillWidth, barH - 4);
    }
    const int knobX = std::max(barX + 2, barX + 2 + fillWidth - 2);
    renderer.fillRect(knobX, barY - 4, 4, barH + 8, true);
    y += barH + gap;

    renderer.drawCenteredText(SMALL_FONT_ID, y, I18N.get(stepHintId_), true);
  }

  bool isActive() const { return active; }

 private:
  int clamp(int v) const { return std::clamp(v, minValue_, maxValue_); }

  // Maps a touch x onto the value range, snapped to the setting's own step so a drag
  // produces the same values the buttons do.
  int valueForX(const int x) const {
    const int usableW = std::max(1, barW_ - 4);
    const int offset = std::clamp(x - (barX_ + 2), 0, usableW);
    const int range = maxValue_ - minValue_;
    const int raw = minValue_ + (offset * range + usableW / 2) / usableW;
    const int step = std::max(1, smallStep_);
    return minValue_ + ((raw - minValue_ + step / 2) / step) * step;
  }

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
  // Where render() last drew the bar, and how far above and below it a touch still counts:
  // the bar itself is only 16 px tall, well under a finger.
  static constexpr int kTouchSlack = 24;
  mutable int barX_ = 0;
  mutable int barY_ = 0;
  mutable int barW_ = 0;
  mutable int barH_ = 0;
};
