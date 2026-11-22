#include "Activity.h"

#include "ActivityManager.h"
#include "components/KeyFocus.h"

void Activity::onEnter() {
  LOG_INF("ACT", "Entering activity: %s", name.c_str());
  // A hint-band tap presses on one frame and releases on the next. If the press
  // brought this screen up, the release belongs to the screen that handled it, not
  // to whatever this one has selected.
  mappedInput.clearHintTap();
  key_focus::keyPressedHere() = false;
}

void Activity::onExit() { LOG_DBG("ACT", "Exiting activity: %s", name.c_str()); }

void Activity::requestUpdate(bool immediate) { activityManager.requestUpdate(immediate); }

void Activity::requestUpdateAndWait() { activityManager.requestUpdateAndWait(); }

void Activity::onGoHome(HomeMenuItem item) { activityManager.goHome(item); }

void Activity::onSelectBook(const std::string& path) { activityManager.goToReader(path); }

void Activity::startActivityForResult(std::unique_ptr<Activity>&& activity, ActivityResultHandler resultHandler) {
  this->resultHandler = std::move(resultHandler);
  activityManager.pushActivity(std::move(activity));
}

void Activity::setResult(ActivityResult&& result) { this->result = std::move(result); }

void Activity::finish() { activityManager.popActivity(); }
