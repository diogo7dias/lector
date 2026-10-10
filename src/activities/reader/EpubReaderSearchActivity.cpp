#include "EpubReaderSearchActivity.h"

#include <Epub/BookSearch.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

void EpubReaderSearchActivity::onEnter() {
  UiListActivity::onEnter();
  searching = matcher.setQuery(query.c_str());
  hits.reserve(16);
  requestUpdate();
}

void EpubReaderSearchActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  hits.clear();
}

void EpubReaderSearchActivity::onHit(void* ctx, const uint32_t offset, const std::string& snippet) {
  auto* self = static_cast<EpubReaderSearchActivity*>(ctx);
  if (self->hits.size() >= MAX_HITS) return;
  RenderLock lock(*self);  // the render task reads `hits` through the published rows
  self->hits.push_back({self->nextSpine, offset, snippet, std::string()});
}

void EpubReaderSearchActivity::searchNextChapter() {
  const int count = epub.getSpineItemsCount();
  if (nextSpine >= count || hits.size() >= MAX_HITS) {
    RenderLock lock(*this);
    searching = false;
    return;
  }
  const size_t before = hits.size();
  book_search::searchSpineItem(epub, nextSpine, matcher, onHit, this);
  if (hits.size() > before) {
    const int tocIndex = epub.getTocIndexForSpineIndex(nextSpine);
    const std::string chapter = tocIndex >= 0 ? epub.getTocItem(tocIndex).title : std::string(tr(STR_UNNAMED));
    RenderLock lock(*this);
    for (size_t i = before; i < hits.size(); ++i) hits[i].chapter = chapter;
  }
  ++nextSpine;
}

void EpubReaderSearchActivity::loop() {
  if (searching) {
    searchNextChapter();
    requestUpdate();
  }
  UiListActivity::loop();
}

int EpubReaderSearchActivity::listCount() const { return static_cast<int>(hits.size()); }

void EpubReaderSearchActivity::buildScreen(UiScreen& screen) {
  if (hits.empty()) {
    screen.centeredText(searching ? tr(STR_SEARCHING) : tr(STR_NO_MATCHES));
    return;
  }
  rows.assign(hits.size(), fui::ListItem{});
  for (size_t i = 0; i < hits.size(); ++i) {
    rows[i].label = hits[i].snippet.c_str();
    rows[i].subtitle = hits[i].chapter.c_str();
    rows[i].actionValue = static_cast<int16_t>(i);
  }
  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

void EpubReaderSearchActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  ProgressChangeResult result{};
  result.spineIndex = hits[index].spineIndex;
  result.hasVisibleTextOffset = true;
  result.visibleTextOffset = hits[index].offset;
  app.clearTapFlash();
  setResult(std::move(result));
  finish();
}

void EpubReaderSearchActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

ListChrome EpubReaderSearchActivity::chrome() const {
  ListChrome chrome;
  chrome.title = query.c_str();
  if (searching) {
    snprintf(progress, sizeof(progress), "%d/%d", nextSpine, epub.getSpineItemsCount());
  } else {
    snprintf(progress, sizeof(progress), "%d", static_cast<int>(hits.size()));
  }
  chrome.headerRight = progress;
  if (hits.empty()) chrome.confirmHint = "";
  return chrome;
}
