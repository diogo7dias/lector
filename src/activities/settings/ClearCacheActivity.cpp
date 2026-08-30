#include "ClearCacheActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/BookCacheUtils.h"

void ClearCacheActivity::onEnter() {
  UiStatusActivity::onEnter();

  state = WARNING;
  const char* options[] = {tr(STR_CANCEL), tr(STR_CLEAR_BUTTON)};
  confirmPopup.show(tr(STR_CLEAR_READING_CACHE), options, 2, 0, [this](int idx) {
    if (idx == 1) {
      beginClear();
    } else {
      goBack();
    }
  });
  requestUpdate();
}

void ClearCacheActivity::onExit() { Activity::onExit(); }

UiStatusActivity::StatusView ClearCacheActivity::statusView() const {
  StatusView view;
  view.title = tr(STR_CLEAR_READING_CACHE);
  switch (state) {
    case WARNING:
      view.lines = {tr(STR_CLEAR_CACHE_WARNING_1), tr(STR_CLEAR_CACHE_WARNING_2), tr(STR_CLEAR_CACHE_WARNING_3),
                    tr(STR_CLEAR_CACHE_WARNING_4)};
      view.backHint = tr(STR_CANCEL);
      view.confirmHint = tr(STR_CLEAR_BUTTON);
      break;
    case CLEARING:
      view.lines = {tr(STR_CLEARING_CACHE), nullptr, nullptr, nullptr};
      // No way out while the card is being written; the hints say so by staying
      // empty rather than offering a button that does nothing.
      view.backHint = "";
      break;
    case SUCCESS:
      view.lines = {tr(STR_CACHE_CLEARED), resultLine.c_str(), nullptr, nullptr};
      break;
    case FAILED:
      view.lines = {tr(STR_CLEAR_CACHE_FAILED), tr(STR_CHECK_SERIAL_OUTPUT), nullptr, nullptr};
      break;
  }
  return view;
}

bool ClearCacheActivity::handleCustomInput() {
  if (state == WARNING) return confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); });
  // The sweep runs on this task; nothing is listening until it ends.
  return state == CLEARING;
}

bool ClearCacheActivity::drawOverlay() { return state == WARNING && confirmPopup.processRender(renderer, mappedInput); }

void ClearCacheActivity::onConfirmButton() {
  if (state == WARNING) beginClear();
}

void ClearCacheActivity::onBackButton() {
  if (state == CLEARING) return;
  goBack();
}

void ClearCacheActivity::beginClear() {
  LOG_DBG("CLEAR_CACHE", "User confirmed, starting cache clear");
  {
    RenderLock lock(*this);
    state = CLEARING;
  }
  requestUpdateAndWait();
  clearCache();
}

void ClearCacheActivity::clearCache() {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    state = FAILED;
    requestUpdate();
    return;
  }

  clearedCount = 0;
  failedCount = 0;
  char name[128];

  // Iterate through all entries in the directory
  for (auto file = root.openNextFile(); file; file = root.openNextFile()) {
    file.getName(name, sizeof(name));
    String itemName(name);

    // Only delete directories matching known book cache names.
    if (file.isDirectory() && isBookCacheDirectoryName(itemName.c_str())) {
      String fullPath = "/.crosspoint/" + itemName;
      LOG_DBG("CLEAR_CACHE", "Removing cache: %s", fullPath.c_str());

      file.close();  // Close before attempting to delete

      if (Storage.removeDir(fullPath.c_str())) {
        clearedCount++;
      } else {
        LOG_ERR("CLEAR_CACHE", "Failed to remove: %s", fullPath.c_str());
        failedCount++;
      }
    } else {
      file.close();
    }
  }
  root.close();

  LOG_DBG("CLEAR_CACHE", "Cache cleared: %d removed, %d failed", clearedCount, failedCount);

  resultLine = std::to_string(clearedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
  if (failedCount > 0) resultLine += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
  state = SUCCESS;
  requestUpdate();
}
