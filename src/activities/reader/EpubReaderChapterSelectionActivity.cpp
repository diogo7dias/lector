#include "EpubReaderChapterSelectionActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

int EpubReaderChapterSelectionActivity::getTotalItems() const { return epub->getTocItemsCount(); }

void EpubReaderChapterSelectionActivity::onEnter() {
  Activity::onEnter();

  if (!epub) {
    return;
  }

  // The reader underneath still pins its page-render glyph arenas. clearCache() is
  // heap-adaptive: below the retention floor it frees them, which is what leaves this
  // list room to keep a whole page of fallback glyphs resident. The reader rebuilds its
  // own arenas on the next page render, at ordinary page-turn cost.
  if (auto* fcm = renderer.getFontCacheManager()) {
    fcm->clearCache();
  }

  selectorIndex = epub->getTocIndexForSpineIndex(currentSpineIndex);
  if (selectorIndex == -1) {
    selectorIndex = 0;
  }

  // Trigger first update
  requestUpdate();
}

void EpubReaderChapterSelectionActivity::refreshTocWindow(const int start, const int pageItems) {
  const int total = getTotalItems();
  const int clamped = std::max(0, std::min(start, total));
  const int count = std::max(0, std::min({pageItems, TOC_WINDOW, total - clamped}));
  if (clamped == windowStart && count == windowCount) return;

  windowStart = clamped;
  windowCount = count;
  for (int i = 0; i < windowCount; i++) {
    windowLabels[i] = tocLabelAt(clamped + i);
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

void EpubReaderChapterSelectionActivity::onExit() { Activity::onExit(); }

void EpubReaderChapterSelectionActivity::loop() {
  const int pageItems = UITheme::getInstance().getNumberOfItemsPerPage(renderer, true, false, true, false);
  const int totalItems = getTotalItems();

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    ActivityResult result;
    result.isCancelled = true;
    setResult(std::move(result));
    finish();
    return;
  }

  auto selectChapter = [this] {
    const auto tocItem = epub->getTocItem(selectorIndex);
    if (tocItem.spineIndex == -1) {
      ActivityResult result;
      result.isCancelled = true;
      setResult(std::move(result));
      finish();
    } else {
      setResult(ChapterResult{tocItem.spineIndex, tocItem.anchor});
      finish();
    }
  };

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    selectChapter();
  }

  buttonNavigator.onNextStep([this, totalItems] {
    selectorIndex = ButtonNavigator::nextIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousStep([this, totalItems] {
    selectorIndex = ButtonNavigator::previousIndex(selectorIndex, totalItems);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::nextPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, totalItems, pageItems] {
    selectorIndex = ButtonNavigator::previousPageIndex(selectorIndex, totalItems, pageItems);
    requestUpdate();
  });
}

void EpubReaderChapterSelectionActivity::render(RenderLock&&) {
  renderer.clearScreen();

  auto metrics = UITheme::getInstance().getMetrics();
  Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);

  GUI.drawHeader(renderer, Rect{screen.x, screen.y + metrics.topPadding, screen.width, metrics.headerHeight},
                 tr(STR_SELECT_CHAPTER));

  const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;

  const int totalItems = getTotalItems();
  // drawList() snaps paging callers to a whole page, so the same arithmetic here makes
  // the cached window cover exactly the rows it is about to ask for.
  const int pageItems = std::max(1, GUI.getListPageItems(contentHeight, false));
  refreshTocWindow(selectorIndex / pageItems * pageItems, pageItems);
  GUI.drawList(renderer, Rect{screen.x, contentTop, screen.width, contentHeight}, totalItems, selectorIndex,
               [this](const int index) {
                 const int offset = index - windowStart;
                 if (offset >= 0 && offset < windowCount) return windowLabels[offset];
                 return tocLabelAt(index);
               });

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
