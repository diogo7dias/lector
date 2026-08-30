#include "XtcReaderChapterSelectionActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

int XtcReaderChapterSelectionActivity::findChapterIndexForPage(const uint32_t page) const {
  if (!xtc) return 0;
  const auto& chapters = xtc->getChapters();
  for (size_t i = 0; i < chapters.size(); i++) {
    if (page >= chapters[i].startPage && page <= chapters[i].endPage) return static_cast<int>(i);
  }
  return 0;
}

void XtcReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();
  if (!xtc) return;
  // Opens on the chapter being read, not at the top.
  moveSelectionTo(findChapterIndexForPage(currentPage));
}

void XtcReaderChapterSelectionActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

int XtcReaderChapterSelectionActivity::listCount() const {
  return xtc ? static_cast<int>(xtc->getChapters().size()) : 0;
}

void XtcReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  if (listCount() == 0) {
    screen.centeredText(tr(STR_NO_CHAPTERS));
    return;
  }

  const auto& chapters = xtc->getChapters();
  rows.assign(chapters.size(), fui::ListItem{});
  for (size_t i = 0; i < chapters.size(); ++i) {
    rows[i].label = chapters[i].name.empty() ? tr(STR_UNNAMED) : chapters[i].name.c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  syncListViewport(screen, props);
  screen.list(props);
}

void XtcReaderChapterSelectionActivity::activateIndex(const int index) {
  if (!xtc) return;
  const auto& chapters = xtc->getChapters();
  if (index < 0 || index >= static_cast<int>(chapters.size())) return;
  app.clearTapFlash();
  setResult(PageResult{chapters[index].startPage});
  finish();
}

void XtcReaderChapterSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
