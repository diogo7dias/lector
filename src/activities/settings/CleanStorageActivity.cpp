#include "CleanStorageActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

void CleanStorageActivity::onEnter() {
  UiStatusActivity::onEnter();

  state = WARNING;
  const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAN_BUTTON)};
  confirmPopup.show(tr(STR_CLEAN_STORAGE), options, 2, 0, [this](int idx) {
    if (idx == 1) {
      beginClean();
    } else {
      goBack();
    }
  });
  requestUpdate();
}

void CleanStorageActivity::onExit() { Activity::onExit(); }

UiStatusActivity::StatusView CleanStorageActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_CLEAN_STORAGE);
  switch (state) {
    case WARNING:
      view.lines = {tr(STR_CLEAN_STORAGE_WARNING_1), tr(STR_CLEAN_STORAGE_WARNING_2), tr(STR_CLEAN_STORAGE_WARNING_3),
                    nullptr};
      view.backHint = tr(STR_CANCEL);
      view.confirmHint = tr(STR_CLEAN_BUTTON);
      break;
    case CLEANING:
      view.lines = {tr(STR_CLEANING_STORAGE), nullptr, nullptr, nullptr};
      view.backHint = "";
      break;
    case SUCCESS:
      view.lines = {tr(STR_STORAGE_CLEANED), resultLine.c_str(), nullptr, nullptr};
      break;
    case FAILED:
      view.lines = {tr(STR_CLEAN_STORAGE_FAILED), tr(STR_CHECK_SERIAL_OUTPUT), nullptr, nullptr};
      break;
  }
  return view;
}

bool CleanStorageActivity::handleCustomInput() {
  if (state == WARNING) return confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); });
  return state == CLEANING;
}

bool CleanStorageActivity::drawOverlay() {
  return state == WARNING && confirmPopup.processRender(renderer, mappedInput);
}

void CleanStorageActivity::onConfirmButton() {
  if (state == WARNING) beginClean();
}

void CleanStorageActivity::onBackButton() {
  if (state == CLEANING) return;
  goBack();
}

void CleanStorageActivity::beginClean() {
  LOG_DBG("CLEAN_STORAGE", "User confirmed, starting orphan sweep");
  {
    RenderLock lock(*this);
    state = CLEANING;
  }
  // The sweep blocks this task for as long as it runs, so the "cleaning" frame has
  // to reach the panel before it starts or the user stares at the warning screen.
  requestUpdateAndWait();
  cleanStorage();
}

void CleanStorageActivity::cleanStorage() {
  // cleanOrphanBookCaches deletes nothing at all unless it could enumerate every
  // book on the card first, because a book it failed to see is indistinguishable
  // from an orphan and its cache holds that book's reading progress.
  if (!cleanOrphanBookCaches(removedCount, keptCount, failedCount)) {
    LOG_ERR("CLEAN_STORAGE", "sweep aborted, nothing removed");
    state = FAILED;
    requestUpdate();
    return;
  }

  resultLine = std::to_string(removedCount) + " " + std::string(tr(STR_ITEMS_REMOVED)) + ", " +
               std::to_string(keptCount) + " " + std::string(tr(STR_ITEMS_KEPT));
  if (failedCount > 0) resultLine += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
  state = SUCCESS;
  requestUpdate();
}
