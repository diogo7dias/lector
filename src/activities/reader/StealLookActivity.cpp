#include "StealLookActivity.h"

#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "activities/ActivityResult.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BookFilingNames.h"

namespace fui = freeink::ui;

void StealLookActivity::loadCandidates() {
  candidates.clear();
  for (const RecentBook& book : RECENT_BOOKS.getBooks()) {
    if (book.path == currentBookPath) continue;  // skip the book we are editing
    const std::string cacheDir = bookfiling::cacheDirFor(book.path);
    if (cacheDir.empty()) continue;
    // Only a book with its own override has a look worth stealing; the rest are just
    // showing the global settings, which this book already follows or has left.
    if (!Storage.exists((cacheDir + "/reader_override.bin").c_str())) continue;
    candidates.push_back({book.title.empty() ? book.path : book.title, book.path, cacheDir});
  }
}

void StealLookActivity::onEnter() {
  loadCandidates();
  UiListActivity::onEnter();
}

void StealLookActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  candidates.clear();
}

const char* StealLookActivity::headerTitle() const { return tr(STR_STEAL_LOOK); }

void StealLookActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  // The base paints the header and the button hints itself, outside the app.
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  if (candidates.empty()) {
    screen.centeredText(tr(STR_STEAL_LOOK_NONE));
    return;
  }

  rows.assign(candidates.size(), fui::ListItem{});
  for (size_t i = 0; i < candidates.size(); ++i) {
    rows[i].label = candidates[i].title.c_str();
    rows[i].icon = listIconFor(UITheme::getFileIcon(candidates[i].path));
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.iconSize = 24;
  syncListViewport(screen, props);
  screen.list(props);
}

void StealLookActivity::activateIndex(const int index) {
  app.clearTapFlash();
  setResult(FilePathResult{candidates[index].cachePath});
  finish();
}

void StealLookActivity::onBackButton() {
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
