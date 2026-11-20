#include "LibraryActivity.h"

#include <Arduino.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryIndexFile.h>
#include <Logging.h>

#include <algorithm>

#include "CrossPointSettings.h"
#include "activities/ActivityManager.h"
#include "activities/RenderLock.h"
#include "components/BusyBanner.h"
#include "components/UiAppHelpers.h"
#include "fontIds.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

namespace fui = freeink::ui;

namespace {
// What the card looked like when the index was last built this boot. Another boot, or
// any book written, renamed or removed since, means the index may be out of date.
bool builtThisBoot = false;
uint32_t builtAtGeneration = 0;

void feedWatchdog(void*) {
  resetTaskWatchdogIfSubscribed();
  busy::tick();
}
}  // namespace

void LibraryActivity::onEnter() {
  // One lock across the base lifecycle and the build: the base schedules a paint, and the
  // render task must not read the index while the walk replaces it.
  RenderLock lock(*this);
  UiListActivity::onEnter();
  refreshIndex(false);
}

void LibraryActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
}

// Rebuild when the index is missing, was built before something changed on the card, or
// `force`. Takes the render lock itself when the caller does not hold it.
void LibraryActivity::refreshIndex(const bool force) {
  const uint32_t generation = Storage.bookGeneration();
  const bool useMetadata = SETTINGS.libraryUseMetadata != 0;
  library::LibraryIndexFile index;
  bool usable = !force && builtThisBoot && builtAtGeneration == generation && index.open(library::libraryIndexPath()) &&
                index.metadataEnabled() == useMetadata;
  if (!usable) {
    index.close();
    BusyBanner banner(renderer, tr(STR_LIBRARY_REBUILDING));
    library::BuildStats stats;
    const uint32_t startMs = millis();
    LOG_INF("LIB", "rebuilding, free heap %u", static_cast<unsigned>(ESP.getFreeHeap()));
    if (library::buildLibraryIndex("/", library::libraryIndexPath(), stats, feedWatchdog, nullptr, useMetadata)) {
      builtThisBoot = true;
      builtAtGeneration = generation;
    }
    LOG_INF("LIB", "rebuilt in %u ms, min free heap %u", static_cast<unsigned>(millis() - startMs),
            static_cast<unsigned>(ESP.getMinFreeHeap()));
    usable = index.open(library::libraryIndexPath());
  }
  count = usable ? index.bookCount() : 0;
  failed = !usable;
  nav.reset();
  requestUpdate();
}

void LibraryActivity::loadWindow(const int start, const int wanted) {
  library::LibraryIndexFile index;
  size_t got = 0;
  if (index.open(library::libraryIndexPath())) {
    got = index.readRecords(static_cast<uint16_t>(start), static_cast<size_t>(wanted), records);
    for (size_t i = 0; i < got; i++) {
      if (!index.readTitle(records[i], titles[i]) || !index.readAuthor(records[i], authors[i])) {
        got = i;
        break;
      }
    }
  }
  if (got < static_cast<size_t>(wanted)) {
    // The index changed or failed under us: list what could be read rather than rows
    // the list would then ask for and find missing.
    LOG_ERR("LIB", "read %u of %d rows at %d", static_cast<unsigned>(got), wanted, start);
    count = static_cast<uint16_t>(start + got);
  }
  for (size_t i = 0; i < got; i++) {
    LOG_DBG("LIB", "row %d: %s | %s", start + static_cast<int>(i), titles[i].c_str(), authors[i].c_str());
  }
  rows.assign(got, fui::ListItem{});
  // One SD pass for the page's fallback glyphs (CJK titles), as the chapter list does.
  struct PrewarmCtx {
    const std::string* titles;
    const std::string* authors;
    size_t rows;
  } ctx{titles, authors, got};
  renderer.prewarmFallbackText(
      UI_10_FONT_ID,
      [](const void* c, const uint32_t i) -> const char* {
        const auto* p = static_cast<const PrewarmCtx*>(c);
        if (i >= p->rows * 2) return nullptr;
        return (i % 2 == 0 ? p->titles[i / 2] : p->authors[i / 2]).c_str();
      },
      &ctx, static_cast<uint32_t>(got * 2));
}

void LibraryActivity::buildScreen(UiScreen& screen) {
  if (count == 0) {
    screen.centeredText(failed ? tr(STR_LIBRARY_REBUILD_FAILED) : tr(STR_LIBRARY_EMPTY));
    return;
  }

  fui::ListProps props{};
  props.count = count;
  props.action = ACTION_ROW;
  // Viewport first: the window has to cover the rows the sync just decided on.
  syncListViewport(screen, props, /*hasSubtitle=*/true);

  const int start = std::max(0, std::min(static_cast<int>(props.topIndex), static_cast<int>(count)));
  const int wanted = std::max(0, std::min({nav.visibleRows + 1, WINDOW, static_cast<int>(count) - start}));
  loadWindow(start, wanted);

  const bool portrait = renderer.getOrientation() == GfxRenderer::Orientation::Portrait;
  for (size_t i = 0; i < rows.size(); ++i) {
    rows[i].label = titles[i].c_str();
    if (!authors[i].empty()) rows[i].subtitle = authors[i].c_str();
    // Icons only in portrait, as in the other lists: the assets cannot be rotated.
    if (portrait) rows[i].icon = listIconFor(UIIcon::Book, 32);
    rows[i].actionValue = static_cast<int16_t>(start + static_cast<int>(i));
  }
  props.count = count;  // loadWindow may have shortened it
  props.items = rows.data();
  props.itemsWindowFirst = static_cast<uint16_t>(start);
  props.itemsWindowCount = static_cast<uint16_t>(rows.size());
  screen.list(props);
}

void LibraryActivity::activateIndex(const int index) {
  if (index < 0 || index >= count) return;
  std::string path;
  {
    // The render task reads the index too; keep it off the card while this handle seeks.
    RenderLock lock(*this);
    library::LibraryIndexFile file;
    library::ClixRecord record;
    if (file.open(library::libraryIndexPath()) && file.readRecord(static_cast<uint16_t>(index), record)) {
      file.readPath(record, path);
    }
    file.close();
    // A row can only name a missing file if something changed the card behind the
    // generation count (the card edited on a computer). Rebuild rather than open nothing.
    if (path.empty() || !Storage.exists(path.c_str())) {
      LOG_INF("LIB", "row %d names a missing book, rebuilding", index);
      refreshIndex(/*force=*/true);
      return;
    }
  }
  LOG_INF("LIB", "open %s", path.c_str());
  app.clearTapFlash();
  activityManager.goToReader(path);
}

ListChrome LibraryActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_LIBRARY);
  if (count == 0) {
    chrome.confirmHint = "";
  } else {
    snprintf(counter, sizeof(counter), tr(STR_LIBRARY_POSITION), std::min<int>(nav.selected, count - 1) + 1,
             static_cast<int>(count));
    chrome.headerRight = counter;
  }
  return chrome;
}
