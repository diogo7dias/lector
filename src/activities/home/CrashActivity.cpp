#include "CrashActivity.h"

#include <HalSystem.h>
#include <I18n.h>

void CrashActivity::onEnter() {
  UiStatusActivity::onEnter();

  panicMessage = HalSystem::getPanicInfo(false);
  if (panicMessage.empty()) {
    panicMessage = tr(STR_CRASH_NO_REASON);
  }
  // Deliberately no clearPanic() here. checkPanic() already marks the capture
  // consumed once the report reached the card, and the next ordinary boot
  // clears the rest in begin(). Clearing it here would also drop a capture
  // whose SD write failed, which is the one case worth keeping.

  requestUpdateAndWait();
}

UiStatusActivity::StatusView CrashActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_CRASH_TITLE);
  view.sections[0].paragraph = tr(STR_CRASH_DESCRIPTION);
  view.sections[0].paragraphMaxLines = 10;
  view.sections[1].heading = tr(STR_CRASH_REASON);
  view.sections[1].paragraph = panicMessage.c_str();
  view.sections[1].paragraphMaxLines = 5;
  return view;
}
