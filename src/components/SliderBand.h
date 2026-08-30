#pragma once
#include <HalGPIO.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/SliderBandGeometry.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ButtonNavigator.h"

// The one control for setting a number. It takes over the screen's header band while a
// value is armed: minus and plus at the ends, the setting's name and its value on the
// line, and a track that answers to a thumb anywhere along it. Nothing below the band
// moves, so a live preview stays where it was and keeps showing what the value does.
//
// Every change applies as it happens. Back and Confirm both just close the band: there is
// no cancel, because the screen behind has been showing the real value the whole time.
class SliderBand {
 public:
  // `band` is the rect the screen reserves for its header, in the same coordinates it
  // hands to drawHeader(). The layout is fixed here so the hit test and the draw can
  // never disagree about where the track is.
  void show(const GfxRenderer& renderer, const std::string& name, const slider_band::Rect& band, const int minValue,
            const int maxValue, const int smallStep, const int largeStep, const int current,
            std::function<void(int)> onChange, std::function<void()> onClose) {
    name_ = name;
    minValue_ = minValue;
    maxValue_ = std::max(minValue, maxValue);
    smallStep_ = std::max(1, smallStep);
    largeStep_ = std::max(smallStep_, largeStep);
    value_ = clamp(current);
    onChange_ = std::move(onChange);
    onClose_ = std::move(onClose);
    layout_ = slider_band::forBand(band.x, band.y, band.width, band.height, renderer.getLineHeight(UI_10_FONT_ID));
    active_ = true;
  }

  bool isActive() const { return active_; }
  int value() const { return value_; }

  void close(const std::function<void()>& requestUpdate) {
    if (!active_) return;
    active_ = false;
    if (onClose_) onClose_();
    requestUpdate();
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

    if (input.wasPressed(MappedInputManager::Button::Back) || input.wasPressed(MappedInputManager::Button::Confirm)) {
      close(requestUpdate);
      return true;
    }

    int tx = 0;
    int ty = 0;
    // A drag owns every pass it lasts, so the level-triggered held query is the right one
    // here; the buttons take the spending query so one contact cannot step twice.
    if (dragging_ && input.isScreenTouchHeld(tx, ty)) {
      setValue(slider_band::valueForX(layout_, tx, minValue_, maxValue_, smallStep_), requestUpdate);
      return true;
    }
    dragging_ = false;
    // Not the spending query: a touch that lands on the track has to keep its contact
    // alive, or the drag it starts would have no samples left to follow. The two buttons
    // and a touch outside spend it by hand once the hit test has ruled.
    if (input.wasScreenTouchDown(tx, ty)) {
      switch (slider_band::hitTest(layout_, tx, ty)) {
        case slider_band::Hit::Minus:
          input.spendTouchContact();
          adjustBy(-smallStep_, requestUpdate);
          return true;
        case slider_band::Hit::Plus:
          input.spendTouchContact();
          adjustBy(smallStep_, requestUpdate);
          return true;
        case slider_band::Hit::Track:
          // Landing anywhere on the track is itself a set, so a tap and the start of a
          // drag are the same gesture and neither needs aiming at the current position.
          dragging_ = true;
          setValue(slider_band::valueForX(layout_, tx, minValue_, maxValue_, smallStep_), requestUpdate);
          return true;
        case slider_band::Hit::None:
          break;
      }
      // A touch anywhere else puts the band away, the same bargain a pop-up makes.
      input.spendTouchContact();
      close(requestUpdate);
      return true;
    }

    // The nav callbacks fire synchronously inside these calls, so capturing requestUpdate
    // by reference is safe (never stored past this handleInput).
    nav_.onPressAndContinuous({MappedInputManager::Button::Left},
                              [this, &requestUpdate] { adjustBy(-smallStep_, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Right},
                              [this, &requestUpdate] { adjustBy(smallStep_, requestUpdate); });
    // On the X3 the side keys sit on the physical left and right edges, so up must be the
    // one that decreases there or the large step would run against the small one.
    const int upDelta = gpio.deviceIsX3() ? -largeStep_ : largeStep_;
    const int downDelta = gpio.deviceIsX3() ? largeStep_ : -largeStep_;
    nav_.onPressAndContinuous({MappedInputManager::Button::Up},
                              [this, upDelta, &requestUpdate] { adjustBy(upDelta, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down},
                              [this, downDelta, &requestUpdate] { adjustBy(downDelta, requestUpdate); });
    return true;
  }

  // Painted in place of the header: one solid band, same ink as the title row it replaces,
  // so an armed value reads as a mode and not as an extra strip of furniture.
  void render(const GfxRenderer& renderer) const {
    if (!active_ || !layout_.valid) return;
    const auto& band = layout_.band;
    renderer.fillRect(band.x, band.y, band.width, band.height, true);

    drawButton(renderer, layout_.minus, "-");
    drawButton(renderer, layout_.plus, "+");

    char valueText[16];
    snprintf(valueText, sizeof(valueText), "%d", value_);
    const int valueWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText);
    const int textY = layout_.text.y;
    const std::string name = renderer.truncatedText(
        UI_10_FONT_ID, name_.c_str(), layout_.text.width - valueWidth - slider_band::kPad * 2, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, layout_.text.x, textY, name.c_str(), false, EpdFontFamily::REGULAR);
    renderer.drawText(UI_10_FONT_ID, layout_.text.x + layout_.text.width - valueWidth, textY, valueText, false,
                      EpdFontFamily::REGULAR);

    // White outline, white fill: the band is already ink, so the track is drawn by taking
    // ink away rather than adding it.
    const auto& track = layout_.track;
    renderer.drawRect(track.x, track.y, track.width, track.height, false);
    const int fill = slider_band::fillWidthFor(layout_, value_, minValue_, maxValue_);
    if (fill > 0) renderer.fillRect(track.x, track.y, fill, track.height, false);
  }

 private:
  void drawButton(const GfxRenderer& renderer, const slider_band::Rect& rect, const char* glyph) const {
    renderer.drawRect(rect.x, rect.y, rect.width, rect.height, false);
    const int glyphWidth = renderer.getTextWidth(UI_10_FONT_ID, glyph);
    const int glyphHeight = renderer.getLineHeight(UI_10_FONT_ID);
    renderer.drawText(UI_10_FONT_ID, rect.x + (rect.width - glyphWidth) / 2, rect.y + (rect.height - glyphHeight) / 2,
                      glyph, false, EpdFontFamily::REGULAR);
  }

  int clamp(const int value) const { return std::clamp(value, minValue_, maxValue_); }

  void setValue(const int next, const std::function<void()>& requestUpdate) {
    const int clamped = clamp(next);
    if (clamped == value_) return;
    value_ = clamped;
    if (onChange_) onChange_(value_);
    requestUpdate();
  }

  void adjustBy(const int delta, const std::function<void()>& requestUpdate) {
    setValue(value_ + delta, requestUpdate);
  }

  bool active_ = false;
  bool dragging_ = false;
  std::string name_;
  int value_ = 0;
  int minValue_ = 0;
  int maxValue_ = 0;
  int smallStep_ = 1;
  int largeStep_ = 5;
  slider_band::Layout layout_;
  std::function<void(int)> onChange_;
  std::function<void()> onClose_;
  ButtonNavigator nav_{120, 350};
};
