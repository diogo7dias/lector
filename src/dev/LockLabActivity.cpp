#ifdef LECTOR_LOCK_LAB

#include "dev/LockLabActivity.h"

#include <GfxRenderer.h>

#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "dev/LockLab.h"

namespace fui = freeink::ui;

void LockLabActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  values.clear();
  result.clear();
}

int LockLabActivity::listCount() const { return static_cast<int>(locklab::knobCount()) + 1; }

bool LockLabActivity::handleCustomInput() {
  if (result.empty()) return false;
  // A run leaves the wallpaper on the panel and the list off it, which is the point: the
  // picture is half of what is being judged. Any button brings the list back.
  if (mappedInput.wasAnyReleased()) {
    result.clear();
    requestUpdate();
  }
  // Consume the pass either way, so a press meant to dismiss the wallpaper cannot also
  // move the selection underneath it.
  return true;
}

void LockLabActivity::activateIndex(const int index) {
  LockLabState& state = APP_STATE.lockLab;
  const int knobs = static_cast<int>(locklab::knobCount());
  if (index < knobs) {
    locklab::cycleKnob(locklab::knobs()[index], state);
    APP_STATE.saveToFile();
    requestUpdate();
    return;
  }

  app.clearTapFlash();
  if (state.realSleep != 0) {
    // A render in isolation is not a lock: it skips the favourites reconcile, the index
    // pick, the frame save and the WiFi teardown. Hand the request to the main loop so
    // the number that comes back is the real trip.
    locklab::requestFullLock();
    finish();
    return;
  }

  char summary[512];
  locklab::runOnce(renderer, summary, sizeof(summary));
  result = summary;
}

void LockLabActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  const LockLabState& state = APP_STATE.lockLab;
  const int count = listCount();
  const int knobs = static_cast<int>(locklab::knobCount());

  values.assign(count, std::string());
  rows.assign(count, fui::ListItem{});
  for (int i = 0; i < knobs; ++i) {
    const locklab::Knob& knob = locklab::knobs()[i];
    rows[i].label = knob.label;
    values[i] = locklab::knobValueName(knob, state);
    rows[i].value = values[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  rows[knobs].label = "Run";
  // The last run's summary rides on the Run row rather than a popup: it is the row the
  // tester is already looking at, and it survives the list rebuild that follows a knob
  // change, so two recipes can be compared without scrolling back through the log.
  values[knobs] = result.empty() ? std::string("render now") : result;
  rows[knobs].value = values[knobs].c_str();
  rows[knobs].actionValue = static_cast<int16_t>(knobs);

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

#endif  // LECTOR_LOCK_LAB
