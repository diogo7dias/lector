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
  const char* options[] = {tr(STR_CANCEL), orphansOnly ? tr(STR_CLEAN_BUTTON) : tr(STR_CLEAR_BUTTON)};
  confirmPopup.show(orphansOnly ? tr(STR_CLEAN_STORAGE) : tr(STR_CLEAR_READING_CACHE), options, 2, 0, [this](int idx) {
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
  view.title = orphansOnly ? tr(STR_CLEAN_STORAGE) : tr(STR_CLEAR_READING_CACHE);
  switch (state) {
    case WARNING:
      if (orphansOnly) {
        view.lines = {tr(STR_CLEAN_STORAGE_WARNING_1), tr(STR_CLEAN_STORAGE_WARNING_2), tr(STR_CLEAN_STORAGE_WARNING_3),
                      nullptr};
      } else {
        view.lines = {tr(STR_CLEAR_CACHE_WARNING_1), tr(STR_CLEAR_CACHE_WARNING_2), tr(STR_CLEAR_CACHE_WARNING_3),
                      tr(STR_CLEAR_CACHE_WARNING_4)};
      }
      view.backHint = tr(STR_CANCEL);
      view.confirmHint = orphansOnly ? tr(STR_CLEAN_BUTTON) : tr(STR_CLEAR_BUTTON);
      break;
    case CLEARING:
      view.lines = {orphansOnly ? tr(STR_CLEANING_STORAGE) : tr(STR_CLEARING_CACHE), nullptr, nullptr, nullptr};
      // No way out while the card is being written; the hints say so by staying
      // empty rather than offering a button that does nothing.
      view.backHint = "";
      break;
    case SUCCESS:
      view.lines = {orphansOnly ? tr(STR_STORAGE_CLEANED) : tr(STR_CACHE_CLEARED), resultLine.c_str(), nullptr,
                    nullptr};
      break;
    case FAILED:
      view.lines = {orphansOnly ? tr(STR_CLEAN_STORAGE_FAILED) : tr(STR_CLEAR_CACHE_FAILED),
                    tr(STR_CHECK_SERIAL_OUTPUT), nullptr, nullptr};
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
  LOG_DBG(orphansOnly ? "CLEAN_STORAGE" : "CLEAR_CACHE", "User confirmed, starting sweep");
  {
    RenderLock lock(*this);
    state = CLEARING;
  }
  // The sweep blocks this task for as long as it runs, so the "clearing" frame has
  // to reach the panel before it starts or the user stares at the warning screen.
  requestUpdateAndWait();
  clearCache();
}

void ClearCacheActivity::clearCache() {
  int removedCount = 0;
  int keptCount = 0;
  int failedCount = 0;
  // cleanOrphanBookCaches deletes nothing at all unless it could enumerate every
  // book on the card first, because a book it failed to see is indistinguishable
  // from an orphan and its cache holds that book's reading progress.
  const bool swept = orphansOnly ? cleanOrphanBookCaches(removedCount, keptCount, failedCount)
                                 : clearAllCaches(removedCount, failedCount);
  if (!swept) {
    if (orphansOnly) {
      LOG_ERR("CLEAN_STORAGE", "sweep aborted, nothing removed");
    }
    state = FAILED;
    requestUpdate();
    return;
  }

  resultLine = std::to_string(removedCount) + " " + std::string(tr(STR_ITEMS_REMOVED));
  if (orphansOnly) resultLine += ", " + std::to_string(keptCount) + " " + std::string(tr(STR_ITEMS_KEPT));
  if (failedCount > 0) resultLine += ", " + std::to_string(failedCount) + " " + std::string(tr(STR_FAILED_LOWER));
  state = SUCCESS;
  requestUpdate();
}

bool ClearCacheActivity::clearAllCaches(int& clearedCount, int& failedCount) {
  LOG_DBG("CLEAR_CACHE", "Clearing cache...");

  // Open .crosspoint directory
  auto root = Storage.open("/.crosspoint");
  if (!root || !root.isDirectory()) {
    LOG_DBG("CLEAR_CACHE", "Failed to open cache directory");
    if (root) root.close();
    return false;
  }

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
  return true;
}
