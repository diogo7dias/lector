#pragma once
#include <HalGPIO.h>

#include <functional>
#include <string>

#include "MappedInputManager.h"
#include "components/SliderField.h"
#include "util/ButtonNavigator.h"

// The one control for setting a number. It takes over the screen's header band
// while a value is armed: minus and plus at the ends, the setting's name and its
// value on the line, and a track that answers to a thumb anywhere along it.
// Nothing below the band moves, so a live preview stays where it was and keeps
// showing what the value does.
//
// This is the state and the keys only. The band is drawn and hit-tested by
// UiGridActivity through FreeInkUI's slider row, so its look comes from the
// theme and its touch comes from the same interaction table as everything else;
// before that it painted itself and carried a second copy of the geometry to
// hit-test against.
//
// Every change applies as it happens. Back and Confirm both just close the band:
// there is no cancel, because the screen behind has been showing the real value
// the whole time.
class SliderBand {
 public:
  void show(const std::string& name, const int minValue, const int maxValue, const int smallStep, const int largeStep,
            const int current, std::function<void(int)> onChange, std::function<void()> onClose) {
    name_ = name;
    range_.min = minValue;
    range_.max = maxValue > minValue ? maxValue : minValue;
    smallStep_ = smallStep > 1 ? smallStep : 1;
    largeStep_ = largeStep > smallStep_ ? largeStep : smallStep_;
    value_ = slider_field::clamp(current, range_);
    onChange_ = std::move(onChange);
    onClose_ = std::move(onClose);
    active_ = true;
  }

  bool isActive() const { return active_; }
  int value() const { return value_; }
  int minValue() const { return range_.min; }
  int maxValue() const { return range_.max; }
  int smallStep() const { return smallStep_; }
  const std::string& name() const { return name_; }

  void close(const std::function<void()>& requestUpdate) {
    if (!active_) return;
    active_ = false;
    if (onClose_) onClose_();
    requestUpdate();
  }

  // A drag along the track: the SDK reports where the finger is as permille of
  // the track's width.
  void setFromPermille(const int permille, const std::function<void()>& requestUpdate) {
    setValue(slider_field::valueForPermille(permille, range_), requestUpdate);
  }

  void adjustBy(const int delta, const std::function<void()>& requestUpdate) {
    setValue(value_ + delta, requestUpdate);
  }

  // The keys only; touch reaches the band through the screen's interaction table.
  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active_) return false;

    if (input.wasPressed(MappedInputManager::Button::Back) || input.wasPressed(MappedInputManager::Button::Confirm)) {
      close(requestUpdate);
      return true;
    }

    // The nav callbacks fire synchronously inside these calls, so capturing
    // requestUpdate by reference is safe (never stored past this handleInput).
    nav_.onPressAndContinuous({MappedInputManager::Button::Left},
                              [this, &requestUpdate] { adjustBy(-smallStep_, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Right},
                              [this, &requestUpdate] { adjustBy(smallStep_, requestUpdate); });
    const auto side = slider_field::sideDeltas(gpio.deviceIsX3(), largeStep_);
    nav_.onPressAndContinuous({MappedInputManager::Button::Up},
                              [this, side, &requestUpdate] { adjustBy(side.up, requestUpdate); });
    nav_.onPressAndContinuous({MappedInputManager::Button::Down},
                              [this, side, &requestUpdate] { adjustBy(side.down, requestUpdate); });
    return true;
  }

 private:
  void setValue(const int next, const std::function<void()>& requestUpdate) {
    const int clamped = slider_field::clamp(next, range_);
    if (clamped == value_) return;
    value_ = clamped;
    if (onChange_) onChange_(value_);
    requestUpdate();
  }

  bool active_ = false;
  std::string name_;
  int value_ = 0;
  slider_field::Range range_{0, 0};
  int smallStep_ = 1;
  int largeStep_ = 5;
  std::function<void(int)> onChange_;
  std::function<void()> onClose_;
  ButtonNavigator nav_{120, 350};
};
