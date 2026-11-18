#include "AllBookmarksActivity.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>

#include "RecentBooksStore.h"
#include "activities/ActivityManager.h"
#include "activities/reader/EpubReaderActivity.h"
#include "components/UiAppHelpers.h"
#include "util/BookmarkFile.h"
#include "util/BookmarkUtil.h"

namespace fui = freeink::ui;

namespace {
// Each row holds a bookmark (its summary and xpath) and the strings drawn for it, about
// 250 bytes, so the C3 keeps the first 64 and the PSRAM board many more.
#if defined(BOARD_HAS_PSRAM)
constexpr size_t MAX_MARKS = 512;
#else
constexpr size_t MAX_MARKS = 64;
#endif

// Files saved before the book's path was stored inside are matched to Recents by name.
const RecentBook* recentFor(const std::string& bookmarkFile) {
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (BookmarkUtil::getBookmarkPath(book.path) == bookmarkFile) return &book;
  }
  return nullptr;
}

std::string titleFromPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  std::string name = path.substr(slash == std::string::npos ? 0 : slash + 1);
  const size_t dot = name.find_last_of('.');
  if (dot != std::string::npos && dot > 0) name.erase(dot);
  return name;
}
}  // namespace

void AllBookmarksActivity::onEnter() {
  UiListActivity::onEnter();
  load();
  requestUpdate();
}

void AllBookmarksActivity::load() {
  const std::string dirPath = BookmarkUtil::getBookmarksDir();
  HalFile dir = Storage.open(dirPath.c_str());
  if (!dir || !dir.isDirectory()) return;

  marks.reserve(MAX_MARKS);
  books.reserve(16);
  titles.reserve(16);
  std::vector<BookmarkEntry> entries;
  char name[256];  // FAT long-file-name maximum (255 chars + terminator)
  for (auto file = dir.openNextFile(); file && marks.size() < MAX_MARKS; file = dir.openNextFile()) {
    const bool isDir = file.isDirectory();
    const size_t nameLen = file.getName(name, sizeof(name));
    file.close();
    if (isDir || nameLen == 0) continue;

    const std::string path = dirPath + name;
    std::string book;
    if (!BookmarkFile::loadFile(path, entries, &book) || entries.empty()) continue;
    const RecentBook* recent = book.empty() ? recentFor(path) : nullptr;
    if (recent) {
      book = recent->path;
      // Stamp the path in, so the book stays listed once it drops out of Recents.
      BookmarkFile::save(book, entries);
    }
    // A book deleted or moved by hand leaves its bookmarks behind; nothing can open them.
    if (book.empty() || !Storage.exists(book.c_str())) continue;

    if (!recent) {
      const auto& recents = RECENT_BOOKS.getBooks();
      const auto it = std::find_if(recents.begin(), recents.end(), [&](const RecentBook& r) { return r.path == book; });
      if (it != recents.end()) recent = &*it;
    }
    titles.push_back(recent && !recent->title.empty() ? recent->title : titleFromPath(book));
    books.push_back(std::move(book));
    const auto bookIndex = static_cast<uint16_t>(books.size() - 1);
    for (auto& entry : entries) {
      if (marks.size() == MAX_MARKS) break;
      marks.push_back({bookIndex, std::move(entry)});
    }
  }
  if (marks.size() == MAX_MARKS) LOG_INF("ABM", "Showing the first %zu bookmarks", MAX_MARKS);

  // Book by book in title order, each book's bookmarks in reading order.
  std::stable_sort(marks.begin(), marks.end(), [this](const Mark& a, const Mark& b) {
    if (a.book != b.book) return titles[a.book] != titles[b.book] ? titles[a.book] < titles[b.book] : a.book < b.book;
    return a.entry.percentage < b.entry.percentage;
  });
  LOG_DBG("ABM", "Loaded %zu bookmarks from %zu books", marks.size(), books.size());
}

void AllBookmarksActivity::onExit() {
  UiListActivity::onExit();
  rows.clear();
  subtitles.clear();
}

void AllBookmarksActivity::buildScreen(UiScreen& screen) {
  if (marks.empty()) {
    screen.centeredText(tr(STR_NO_BOOKMARKS));
    return;
  }

  const int count = listCount();
  subtitles.assign(count, std::string());
  rows.assign(count, fui::ListItem{});
  const bool portrait = renderer.getOrientation() == GfxRenderer::Orientation::Portrait;
  for (int i = 0; i < count; ++i) {
    const Mark& mark = marks[i];
    const int percent = static_cast<int>(std::clamp(mark.entry.percentage, 0.0f, 1.0f) * 100.0f + 0.5f);
    subtitles[i] = std::to_string(percent) + "%";
    // A page with no text (a cover) saves no summary, so the title takes the label line.
    if (mark.entry.summary.empty()) {
      rows[i].label = titles[mark.book].c_str();
    } else {
      subtitles[i] += " - " + titles[mark.book];
      rows[i].label = mark.entry.summary.c_str();
    }
    rows[i].subtitle = subtitles[i].c_str();
    // Icons only in portrait, as in the book's own list: the assets cannot be rotated.
    if (portrait) rows[i].icon = listIconFor(UIIcon::Bookmark, 32);
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

void AllBookmarksActivity::activateIndex(const int index) {
  if (index < 0 || index >= listCount()) return;
  const Mark& mark = marks[index];
  EpubReaderActivity::queueBookmarkJump(books[mark.book], mark.entry);
  activityManager.goToReader(books[mark.book]);
}

ListChrome AllBookmarksActivity::chrome() const {
  ListChrome chrome;
  chrome.title = tr(STR_ALL_BOOKMARKS);
  if (marks.empty()) chrome.confirmHint = "";
  return chrome;
}
