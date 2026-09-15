#include "ButtonNavigator.h"

#include <algorithm>

const MappedInputManager* ButtonNavigator::mappedInput = nullptr;

void ButtonNavigator::onNext(const Callback& callback) {
  onNextPress(callback);
  onNextContinuous(callback);
}

void ButtonNavigator::onPrevious(const Callback& callback) {
  onPreviousPress(callback);
  onPreviousContinuous(callback);
}

void ButtonNavigator::onPressAndContinuous(const Buttons& buttons, const Callback& callback) {
  onPress(buttons, callback);
  onContinuous(buttons, callback);
}

void ButtonNavigator::onNextPress(const Callback& callback) { onPress(getNextButtons(), callback); }

void ButtonNavigator::onPreviousPress(const Callback& callback) { onPress(getPreviousButtons(), callback); }

void ButtonNavigator::onNextRelease(const Callback& callback) { onRelease(getNextButtons(), callback); }

void ButtonNavigator::onPreviousRelease(const Callback& callback) { onRelease(getPreviousButtons(), callback); }

void ButtonNavigator::onRelease(const Buttons& buttons, const Callback& callback) {
  // Swipes belong to onContinuous; release-stepping lists call both helpers.
  // Dispatching here too would move a row before the swipe moves a page.
  const bool released = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });
  if (!released) return;
  // A release that ends a repeat run moves nothing: the hold already did the moving.
  if (lastContinuousNavTime == 0) callback();
  lastContinuousNavTime = 0;
  repeatIndex_ = 0;
}

void ButtonNavigator::onNextStep(const Callback& callback) { onStep(getNextButtons(), callback); }

void ButtonNavigator::onPreviousStep(const Callback& callback) { onStep(getPreviousButtons(), callback); }

void ButtonNavigator::onNextContinuous(const Callback& callback) { onContinuous(getNextButtons(), callback); }

void ButtonNavigator::onPreviousContinuous(const Callback& callback) { onContinuous(getPreviousButtons(), callback); }

void ButtonNavigator::onPress(const Buttons& buttons, const Callback& callback) {
  const bool wasPressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });

  if (wasPressed) {
    callback();
  }
}

void ButtonNavigator::onStep(const Buttons& buttons, const Callback& callback) {
  // One step the instant the button goes down. Holding then repeats through
  // onContinuous, which only starts after continuousStartMs, so the press and the
  // first repeat can never fire in the same hold.
  const bool pressed = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasPressed(button);
  });
  if (pressed) callback();

  // The release itself no longer moves anything; it only ends a repeat run so the
  // next hold has to earn its delay again.
  const bool released = std::any_of(buttons.begin(), buttons.end(), [](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->wasReleased(button);
  });
  if (released) {
    lastContinuousNavTime = 0;
    repeatIndex_ = 0;
  }
}

void ButtonNavigator::resetRowTap() {
  rowTapDetector_.reset();
  rowTapSteps_ = 1;
}

void ButtonNavigator::onRowTap(const MappedInputManager::Button button, const std::function<void(int)>& callback,
                               const bool onRelease) {
  if (mappedInput == nullptr) return;
  const bool pressed = mappedInput->wasPressed(button);
  const bool released = mappedInput->wasReleased(button);
  if (!pressed && !released) return;
  if (rowTapButton_ != button) resetRowTap();
  rowTapButton_ = button;
  rowTapDetector_.configure(true, false);
  int steps = 0;
  if (pressed) {
    rowTapSteps_ = rowTapDetector_.onPress(millis()) == button_gestures::Event::Double ? 4 : 1;
    if (!onRelease) steps = rowTapSteps_;
  }
  if (released) {
    if (lastContinuousNavTime == 0) {
      if (onRelease) steps = rowTapSteps_;
      rowTapDetector_.onRelease(millis());
    }
    // A hold cannot seed a double tap, even if its release arrived before the
    // loop had a chance to repeat. Its existing movement is left alone.
    if (lastContinuousNavTime != 0 || mappedInput->getHeldTime() > continuousStartMs) resetRowTap();
    lastContinuousNavTime = 0;
    repeatIndex_ = 0;
    rowTapSteps_ = 1;
  }
  if (steps != 0) callback(steps);
}

// A body swipe drives the same movement the nav buttons do, so every list screen
// scrolls under the finger without its own coordinate handling. It is wired to the
// continuous step (a page or a section on the screens that define one) because a
// swipe is a travel gesture, and because firing it here keeps a screen that calls
// onNext() — press plus continuous — from moving twice on one swipe.
bool ButtonNavigator::swipeMatches(const Buttons& buttons) {
  if (mappedInput == nullptr) return false;
  const auto scroll = mappedInput->wasListScrollSwipe();
  if (scroll == list_swipe::Scroll::None) return false;
  const bool wantsNext =
      std::find(buttons.begin(), buttons.end(), MappedInputManager::Button::NavNext) != buttons.end();
  const bool wantsPrevious =
      std::find(buttons.begin(), buttons.end(), MappedInputManager::Button::NavPrevious) != buttons.end();
  return scroll == list_swipe::Scroll::PageDown ? wantsNext : wantsPrevious;
}

void ButtonNavigator::onContinuous(const Buttons& buttons, const Callback& callback) {
  if (swipeMatches(buttons)) {
    resetRowTap();
    callback();
    return;
  }
  const bool isPressed = std::any_of(buttons.begin(), buttons.end(), [this](const MappedInputManager::Button button) {
    return mappedInput != nullptr && mappedInput->isPressed(button) && shouldNavigateContinuously();
  });

  if (isPressed) {
    resetRowTap();
    callback();
    lastContinuousNavTime = millis();
    // Counted after the callback, so the first repeat of a hold is index 0 and
    // holdRepeatStep() starts it fine-grained.
    ++repeatIndex_;
  }
}

bool ButtonNavigator::shouldNavigateContinuously() const {
  if (!mappedInput) return false;

  const bool buttonHeldLongEnough = mappedInput->getHeldTime() > continuousStartMs;
  const bool navigationIntervalElapsed = (millis() - lastContinuousNavTime) > continuousIntervalMs;

  return buttonHeldLongEnough && navigationIntervalElapsed;
}

int ButtonNavigator::nextIndex(const int currentIndex, const int totalItems, const int steps) {
  if (totalItems <= 0) return 0;

  // Calculate the next index with wrap-around
  return (currentIndex + steps % totalItems) % totalItems;
}

int ButtonNavigator::previousIndex(const int currentIndex, const int totalItems, const int steps) {
  if (totalItems <= 0) return 0;

  // Calculate the previous index with wrap-around
  return (currentIndex + totalItems - steps % totalItems) % totalItems;
}

int ButtonNavigator::heldIndex(const int currentIndex, const int totalItems, const int delta) {
  if (totalItems <= 0) return 0;
  return std::clamp(currentIndex + delta, 0, totalItems - 1);
}

bool ButtonNavigator::swipeDrivenPass() { return swipeMatches(getNextButtons()) || swipeMatches(getPreviousButtons()); }

int ButtonNavigator::nextPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return nextIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex < lastPageIndex) {
    return (currentPageIndex + 1) * itemsPerPage;
  }

  return 0;
}

int ButtonNavigator::previousPageIndex(const int currentIndex, const int totalItems, const int itemsPerPage) {
  if (totalItems <= 0 || itemsPerPage <= 0) return 0;

  // When items fit on one page, use index navigation instead
  if (totalItems <= itemsPerPage) {
    return previousIndex(currentIndex, totalItems);
  }

  const int lastPageIndex = (totalItems - 1) / itemsPerPage;
  const int currentPageIndex = currentIndex / itemsPerPage;

  if (currentPageIndex > 0) {
    return (currentPageIndex - 1) * itemsPerPage;
  }

  return lastPageIndex * itemsPerPage;
}
