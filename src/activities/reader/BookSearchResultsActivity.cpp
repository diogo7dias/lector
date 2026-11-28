#include "BookSearchResultsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>

#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
constexpr int LABEL_TAG_WIDTH_DIVISOR = 4;
constexpr uint8_t MAX_SNIPPET_LINES = 2;
}  // namespace

void BookSearchResultsActivity::onEnter() {
  UiListActivity::onEnter();
  requestUpdate();
}

void BookSearchResultsActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  labelTags.clear();
}

void BookSearchResultsActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  const auto& hit = items[static_cast<size_t>(index)];
  ProgressChangeResult res;
  res.spineIndex = hit.spineIndex;
  res.hasVisibleTextOffset = true;
  res.visibleTextOffset = hit.offset;
  ActivityResult result;
  result.isCancelled = false;
  result.data = std::move(res);
  setResult(std::move(result));
  finish();
}

void BookSearchResultsActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}

ListChrome BookSearchResultsActivity::chrome() const {
  headerText = std::string(tr(STR_EDIT_SEARCH)) + " " + query;
  char buf[16];
  snprintf(buf, sizeof(buf), truncated ? "%u+" : "%u", static_cast<unsigned>(items.size()));
  countText = buf;

  ListChrome c;
  c.title = headerText.c_str();
  c.headerRight = countText.c_str();
  c.confirmHint = items.empty() ? "" : tr(STR_OPEN);
  return c;
}

void BookSearchResultsActivity::buildScreen(UiScreen& screen) {
  const int count = listCount();
  if (count == 0) {
    screen.centeredText(tr(STR_NO_SEARCH_RESULTS));
    return;
  }

  const int tagMaxWidth = std::max(1, renderer.getScreenWidth() / LABEL_TAG_WIDTH_DIVISOR);
  labelTags.assign(static_cast<size_t>(count), std::string());
  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    const auto& hit = items[static_cast<size_t>(i)];
    rows[i].label = hit.snippet.c_str();
    if (!hit.label.empty()) {
      labelTags[i] = renderer.truncatedText(UI_10_FONT_ID, hit.label.c_str(), tagMaxWidth, EpdFontFamily::REGULAR);
      rows[i].value = labelTags[i].c_str();
    }
    rows[i].actionValue = static_cast<int16_t>(i);
  }

  fui::ListProps props{};
  props.items = rows.data();
  props.count = static_cast<uint16_t>(rows.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = MAX_SNIPPET_LINES;
  syncListViewport(screen, props);
  screen.list(props);
}
