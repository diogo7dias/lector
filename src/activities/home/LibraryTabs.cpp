#include <Arduino.h>
#include <FsHelpers.h>
#include <I18n.h>
#include <LibraryBuilder.h>
#include <LibraryGroups.h>
#include <LibraryIndexFile.h>
#include <Utf8.h>

#include <algorithm>
#include <cstdio>

#include "LibraryActivity.h"
#include "activities/RenderLock.h"
#include "activities/util/BookInfoHelper.h"
#include "activities/util/ConfirmationActivity.h"
#include "components/BusyBanner.h"
#include "util/BookCacheUtils.h"
#include "util/BookFilingNames.h"
#include "util/BusyTick.h"
#include "util/TaskWatchdog.h"

namespace {
constexpr unsigned long HOLD_ACTIONS_MS = 700;

void tabWatchdog(void*) {
  resetTaskWatchdogIfSubscribed();
  busy::tick();
}
}  // namespace

void LibraryActivity::tabActionTrampoline(const freeink::ui::ActionEvent& event, void* user) {
  auto* self = static_cast<LibraryActivity*>(user);
  if (event.value < 0 || event.value >= static_cast<int>(LibraryTab::Count)) return;
  self->switchTab(static_cast<LibraryTab>(event.value));
}

void LibraryActivity::switchTab(const LibraryTab tab) {
  if (tab == activeTab && activeGroup.empty()) return;
  {
    RenderLock lock(*this);
    activeTab = tab;
    activeGroup.clear();
    groups.clear();
    searchQuery.clear();
    hits.clear();
    totalHits = 0;
    if (tab == LibraryTab::Books) {
      rebuildView();
    } else {
      viewOrder.clear();
      viewActive = false;
      BusyBanner banner(renderer, tr(STR_LOADING));
      library::LibraryIndexFile index;
      if (index.open(library::libraryIndexPath())) {
        const auto kind = tab == LibraryTab::Authors ? library::GroupKind::Authors : library::GroupKind::Series;
        library::buildGroups(index, kind, groups, tabWatchdog, nullptr);
      }
      recount();
    }
    nav.reset();
  }
  requestUpdate();
}

void LibraryActivity::openGroup(const int groupIndex) {
  if (groupIndex < 0 || groupIndex >= static_cast<int>(groups.size())) return;
  {
    RenderLock lock(*this);
    groupReturnRow = groupIndex;
    activeGroup = groups[static_cast<size_t>(groupIndex)].name;
    viewOrder.clear();
    viewActive = true;
    library::LibraryIndexFile index;
    if (index.open(library::libraryIndexPath())) {
      const auto kind = activeTab == LibraryTab::Authors ? library::GroupKind::Authors : library::GroupKind::Series;
      library::collectBooksInGroup(index, kind, activeGroup, viewOrder, tabWatchdog, nullptr);
    }
    recount();
    nav.reset();
  }
  requestUpdate();
}

void LibraryActivity::buildGroupListScreen(UiScreen& screen) {
  namespace fui = freeink::ui;
  if (groups.empty()) {
    screen.centeredText(activeTab == LibraryTab::Authors ? tr(STR_NO_AUTHORS) : tr(STR_NO_SERIES));
    return;
  }
  fui::ListProps props{};
  props.count = static_cast<uint16_t>(groups.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  syncListViewport(screen, props, /*hasSubtitle=*/false);

  const int total = static_cast<int>(groups.size());
  const int start = std::max(0, std::min(static_cast<int>(props.topIndex), total));
  const int shown = std::max(0, std::min({nav.visibleRows + 1, WINDOW, total - start}));
  rows.assign(static_cast<size_t>(shown), fui::ListItem{});
  for (int i = 0; i < shown; ++i) {
    const int idx = start + i;
    const auto& g = groups[static_cast<size_t>(idx)];
    rows[i].actionValue = static_cast<int16_t>(idx);
    rows[i].label = g.name.c_str();
    snprintf(badges[i], sizeof(badges[i]), tr(STR_LIBRARY_COUNT), static_cast<int>(g.count));
    rows[i].value = badges[i];
  }
  props.items = rows.data();
  props.itemsWindowFirst = static_cast<uint16_t>(start);
  props.itemsWindowCount = static_cast<uint16_t>(rows.size());
  screen.list(props);
}

void LibraryActivity::onRowLongPress(const int index) {
  if (showingGroups() || index < headerRows() || index >= rowCount) return;
  std::string path;
  library::ClixRecord record{};
  {
    RenderLock lock(*this);
    library::LibraryIndexFile file;
    if (file.open(library::libraryIndexPath()) &&
        file.readRecord(static_cast<uint16_t>(ordinalAt(index - headerRows())), record)) {
      file.readPath(record, path);
    }
    file.close();
    if (path.empty() || !Storage.exists(path.c_str())) {
      refreshIndex(/*force=*/true);
      return;
    }
  }

  StrId actions[3];
  int count = 0;
  if (FsHelpers::hasEpubExtension(path)) actions[count++] = StrId::STR_BOOK_INFO;
  if (FsHelpers::hasEpubExtension(path) || FsHelpers::hasTextBookExtension(path)) {
    actions[count++] = library::isRecordRead(record) ? StrId::STR_MARK_UNREAD : StrId::STR_MARK_READ;
  }
  actions[count++] = StrId::STR_DELETE;

  app.clearTapFlash();
  bookActionPopup.show(StrId::STR_FILE_ACTIONS, actions, count, 0, [this, path, actions](const int choice) {
    switch (actions[choice]) {
      case StrId::STR_BOOK_INFO:
        openBookInfoScreen(*this, renderer, mappedInput, path, [this] { requestUpdate(); });
        break;
      case StrId::STR_MARK_READ:
      case StrId::STR_MARK_UNREAD: {
        const bool ok = actions[choice] == StrId::STR_MARK_READ ? markBookRead(path) : markBookUnread(path);
        if (ok) {
          RenderLock lock(*this);
          rebuildView();
          if (nav.selected >= rowCount && rowCount > 0) nav.selected = rowCount - 1;
        }
        requestUpdate();
        break;
      }
      default:
        confirmDelete(path);
        break;
    }
  });
  requestUpdate();
}

void LibraryActivity::confirmDelete(const std::string& fullPath) {
  const std::string heading = tr(STR_DELETE) + std::string("? ");
  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, heading,
                                             utf8ComposeNfc(std::string(bookfiling::fileNameOf(fullPath)))),
      [this, fullPath](const ActivityResult& res) {
        if (!res.isCancelled) {
          if (deleteOpenedBook(fullPath)) {
            RenderLock lock(*this);
            refreshIndex(false);
          } else {
            GUI.drawPopup(renderer, tr(STR_DELETE_FAILED));
            delay(1200);
            requestUpdate(true);
          }
        } else {
          requestUpdate();
        }
      });
}

bool LibraryActivity::handleCustomInput() {
  if (bookActionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (!mappedInput.hasTouch() && bookCount > 0 && !inGroupDetail() &&
      (showingGroups() || (activeTab == LibraryTab::Books && nav.selected == 0))) {
    const int delta = mappedInput.wasPressed(MappedInputManager::Button::Left)    ? -1
                      : mappedInput.wasPressed(MappedInputManager::Button::Right) ? 1
                                                                                  : 0;
    if (delta != 0) {
      constexpr int kTabs = static_cast<int>(LibraryTab::Count);
      switchTab(static_cast<LibraryTab>((static_cast<int>(activeTab) + delta + kTabs) % kTabs));
      return true;
    }
  }
  return false;
}

bool LibraryActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  switch (confirmHold.update(mappedInput.wasPressed(MappedInputManager::Button::Confirm),
                             mappedInput.isPressed(MappedInputManager::Button::Confirm),
                             mappedInput.wasReleased(MappedInputManager::Button::Confirm), mappedInput.getHeldTime(),
                             HOLD_ACTIONS_MS)) {
    case hold_button::Fired::Hold:
      confirmHold.reset();
      if (nav.selected >= headerRows() && nav.selected < rowCount) onRowLongPress(nav.selected);
      return true;
    case hold_button::Fired::Short:
      if (nav.selected >= 0 && nav.selected < rowCount) activateIndex(nav.selected);
      return true;
    case hold_button::Fired::None:
      break;
  }
  return false;
}
