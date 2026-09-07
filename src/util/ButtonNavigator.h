#pragma once

#include <functional>
#include <vector>

#include "MappedInputManager.h"

class ButtonNavigator final {
  using Callback = std::function<void()>;
  using Buttons = std::vector<MappedInputManager::Button>;

  const uint16_t continuousStartMs;
  const uint16_t continuousIntervalMs;
  uint32_t lastContinuousNavTime = 0;
  // Repeats this hold has already fired, so a list can ramp its step the way a
  // numeric setting does (see util/HoldRepeat.h). Reset wherever
  // lastContinuousNavTime is, i.e. on the release that ends the run.
  unsigned repeatIndex_ = 0;
  static const MappedInputManager* mappedInput;

  [[nodiscard]] bool shouldNavigateContinuously() const;
  [[nodiscard]] static bool swipeMatches(const Buttons& buttons);

 public:
  // Auto-repeat for a LIST, as opposed to the {500, 500} page-flick default the
  // grid and menu screens were built around. A held key has to travel a long
  // list without becoming unaimable: one row per repeat is the unit, and
  // holdRepeatStep() coarsens it once the hold plainly is not a nudge. 200 ms is
  // the row rate that follows; 400 ms before the first repeat keeps a deliberate
  // single press from ever being read as the start of a run.
  static constexpr uint16_t LIST_REPEAT_INTERVAL_MS = 200;
  static constexpr uint16_t LIST_REPEAT_START_MS = 400;

  explicit ButtonNavigator(const uint16_t continuousIntervalMs = 500, const uint16_t continuousStartMs = 500)
      : continuousStartMs(continuousStartMs), continuousIntervalMs(continuousIntervalMs) {}

  // Repeats fired so far by the hold in progress; 0 on the first one. Feeds
  // holdRepeatStep() so a list ramps from one row to a chunk.
  [[nodiscard]] unsigned repeats() const { return repeatIndex_; }

  // True when this pass's continuous callback was driven by a body swipe rather
  // than a held key. A swipe is a travel gesture and still moves a page; a hold
  // moves rows. Without this the two would share one step and the finger would
  // scroll a list one row at a time.
  [[nodiscard]] static bool swipeDrivenPass();

  static void setMappedInputManager(const MappedInputManager& mappedInputManager) { mappedInput = &mappedInputManager; }

  void onNext(const Callback& callback);
  void onPrevious(const Callback& callback);
  void onPressAndContinuous(const Buttons& buttons, const Callback& callback);

  void onNextPress(const Callback& callback);
  void onPreviousPress(const Callback& callback);
  void onPress(const Buttons& buttons, const Callback& callback);

  // One step on the RELEASE, suppressed when a hold already repeated. Used by the
  // FreeInkUI list host, where a hold pages and so must not also step on the way up.
  void onNextRelease(const Callback& callback);
  void onPreviousRelease(const Callback& callback);
  void onRelease(const Buttons& buttons, const Callback& callback);

  // One step on the press, then auto-repeat while held (see onContinuous).
  void onNextStep(const Callback& callback);
  void onPreviousStep(const Callback& callback);
  void onStep(const Buttons& buttons, const Callback& callback);

  void onNextContinuous(const Callback& callback);
  void onPreviousContinuous(const Callback& callback);
  void onContinuous(const Buttons& buttons, const Callback& callback);

  [[nodiscard]] static int nextIndex(int currentIndex, int totalItems);
  [[nodiscard]] static int previousIndex(int currentIndex, int totalItems);

  // Move `delta` rows for one repeat of a held key. Clamped, never wrapped: a
  // hold that wrapped past the last row would run forever and could not be
  // aimed, which is the whole complaint about holding in a long list. Landing
  // on the first or last row ends the travel. A single press still wraps via
  // nextIndex/previousIndex, which is the behaviour those screens always had.
  [[nodiscard]] static int heldIndex(int currentIndex, int totalItems, int delta);

  [[nodiscard]] static int nextPageIndex(int currentIndex, int totalItems, int itemsPerPage);
  [[nodiscard]] static int previousPageIndex(int currentIndex, int totalItems, int itemsPerPage);

  // Navigation uses the logical NavNext / NavPrevious buttons; MappedInputManager::mapButton resolves
  // them to physical buttons and applies any orientation-based direction swap, so this stays settings-free.
  [[nodiscard]] static Buttons getNextButtons() { return {MappedInputManager::Button::NavNext}; }
  [[nodiscard]] static Buttons getPreviousButtons() { return {MappedInputManager::Button::NavPrevious}; }
};
