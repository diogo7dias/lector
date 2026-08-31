#include "Activity.h"

#include <PerfLog.h>

#include "ActivityManager.h"

void Activity::onEnter() {
  LOG_DBG("ACT", "Entering activity: %s", name.c_str());
  // Commit the outgoing screen's records before the new screen's begin. A screen change
  // is a natural gap between refreshes, so the card write does not land inside anything
  // being timed, and it means a power-off keeps everything up to the last screen change
  // rather than only whole batches.
  PerfLog::flush();
  // Tags the refreshes that follow with the screen that caused them. PerfLog copies the
  // name: this activity is deleted on exit, so a stored pointer would dangle.
  PerfLog::setScreen(name.c_str());
  // A hint-band tap presses on one frame and releases on the next. If the press
  // brought this screen up, the release belongs to the screen that handled it, not
  // to whatever this one has selected.
  mappedInput.clearHintTap();
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
