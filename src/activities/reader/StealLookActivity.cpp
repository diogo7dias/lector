#include "StealLookActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/ActivityResult.h"
#include "components/ListWindowRefresh.h"
#include "components/UITheme.h"
#include "fontIds.h"

void StealLookActivity::loadCandidates() {
  candidates.clear();
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path == currentBookPath) continue;  // skip the book we are editing
    const std::string cacheDir = RecentBooksStore::bookCacheDir(book.path);
    if (cacheDir.empty()) continue;
    // Only books with their own reader override (custom look) are stealable.
    if (!Storage.exists((cacheDir + "/reader_override.bin").c_str())) continue;
    candidates.push_back({book.title.empty() ? book.path : book.title, book.path, cacheDir});
  }
}

void StealLookActivity::onEnter() {
  Activity::onEnter();
  loadCandidates();
  selectorIndex = 0;
  requestUpdate();
}

void StealLookActivity::onExit() {
  Activity::onExit();
  candidates.clear();
}

void StealLookActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, true);

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (!candidates.empty() && selectorIndex < candidates.size()) {
      setResult(FilePathResult{candidates[selectorIndex].cachePath, ""});
      finish();
      return;
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    ActivityResult res;
    res.isCancelled = true;
    setResult(std::move(res));
    finish();
    return;
  }

  const int listSize = static_cast<int>(candidates.size());

  buttonNavigator.onNextPress([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onPreviousPress([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });
  buttonNavigator.onNextContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
  buttonNavigator.onPreviousContinuous([this, listSize, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });
}

void StealLookActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_STEAL_LOOK));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  if (candidates.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_STEAL_LOOK_NONE));
  } else {
    GUI.drawList(
        renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(candidates.size()),
        static_cast<int>(selectorIndex), [this](int index) { return candidates[index].title; },
        [](int) { return std::string(); }, [this](int index) { return UITheme::getFileIcon(candidates[index].path); });
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  list_window::present(renderer);
}
