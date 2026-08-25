#include "EpubReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

int EpubReaderChapterSelectionActivity::listCount() const { return epub ? epub->getTocItemsCount() : 0; }

const char* EpubReaderChapterSelectionActivity::headerTitle() const { return tr(STR_SELECT_CHAPTER); }

void EpubReaderChapterSelectionActivity::onEnter() {
  UiListActivity::onEnter();

  if (!epub) return;

  // The reader underneath still pins its page-render glyph arenas. clearCache() is
  // heap-adaptive: below the retention floor it frees them, which is what leaves this
  // list room to keep a whole page of fallback glyphs resident. The reader rebuilds its
  // own arenas on the next page render, at ordinary page-turn cost.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  const int selected = epub->getTocIndexForSpineIndex(currentSpineIndex);
  moveSelectionTo(selected == -1 ? 0 : selected);
}

void EpubReaderChapterSelectionActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  windowStart = -1;
  windowCount = 0;
}

void EpubReaderChapterSelectionActivity::refreshTocWindow(const int start, const int count) {
  if (start == windowStart && count == windowCount) return;

  windowStart = start;
  windowCount = count;
  for (int i = 0; i < windowCount; i++) {
    windowLabels[i] = tocLabelAt(start + i);
  }

  // One SD pass for the whole visible page. The getter form is deliberate: building a
  // concatenated string here would allocate on a heap this screen is already tight on.
  struct PrewarmCtx {
    const std::string* labels;
    int count;
  } ctx{windowLabels, windowCount};
  renderer.prewarmFallbackText(
      UI_10_FONT_ID,
      [](const void* c, const uint32_t i) -> const char* {
        const auto* p = static_cast<const PrewarmCtx*>(c);
        return i < static_cast<uint32_t>(p->count) ? p->labels[i].c_str() : nullptr;
      },
      &ctx, static_cast<uint32_t>(windowCount));
}

std::string EpubReaderChapterSelectionActivity::tocLabelAt(const int index) const {
  const auto item = epub->getTocItem(index);
  // level 0 exists in malformed TOCs; (level - 1) * 2 would be negative and the indent
  // string would be asked for a nonsense length.
  const std::string indent(item.level > 0 ? (item.level - 1) * 2 : 0, ' ');
  return indent + item.title;
}

void EpubReaderChapterSelectionActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing), 0,
                  static_cast<int16_t>(metrics.buttonHintsHeight + metrics.verticalSpacing), 0});

  const int total = listCount();
  if (total <= 0) return;

  fui::ListProps props{};
  props.count = static_cast<uint16_t>(total);
  props.action = ACTION_ROW;
  // Viewport first: the window has to cover the rows the sync just decided on.
  syncListViewport(screen, props);

  const int start = std::max(0, std::min(static_cast<int>(props.topIndex), total));
  // One row past the measured page: list() lays out a partial row at the bottom band
  // when one fits, and it reads its label like any other.
  const int count = std::max(0, std::min({nav.visibleRows + 1, TOC_WINDOW, total - start}));
  refreshTocWindow(start, count);

  rows.assign(static_cast<size_t>(count), fui::ListItem{});
  for (int i = 0; i < count; ++i) {
    rows[i].label = windowLabels[i].c_str();
    rows[i].actionValue = static_cast<int16_t>(start + i);
  }
  props.items = rows.data();
  props.itemsWindowFirst = static_cast<uint16_t>(start);
  props.itemsWindowCount = static_cast<uint16_t>(count);
  screen.list(props);
}

void EpubReaderChapterSelectionActivity::activateIndex(const int index) {
  app.clearTapFlash();
  const auto tocItem = epub->getTocItem(index);
  if (tocItem.spineIndex == -1) {
    onBackButton();
    return;
  }
  setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
  finish();
}

void EpubReaderChapterSelectionActivity::onBackButton() {
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  finish();
}
