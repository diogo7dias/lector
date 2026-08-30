#pragma once
#include <Epub.h>

#include <memory>
#include <string>
#include <vector>

#include "../../BookmarkEntry.h"
#include "../UiListActivity.h"
#include "components/OptionPopup.h"

class EpubReaderBookmarksActivity final : public UiListActivity {
  std::shared_ptr<Epub> epub;
  std::string epubPath;
  std::vector<BookmarkEntry> bookmarks;
  OptionPopup confirmPopup;

  // The rows and the strings behind them: buildScreen only hands out pointers,
  // so both have to outlive it.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> subtitles;
  void refreshRows(bool portrait);

  /** Asks before deleting; the answer runs deleteBookmark(). */
  void promptDelete(int index);
  void deleteBookmark(int index);
  void openBookmark(int index);

 public:
  explicit EpubReaderBookmarksActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                       const std::shared_ptr<Epub>& epub, const std::string& epubPath)
      : UiListActivity("EpubReaderBookmarks", renderer, mappedInput, /*wantsTouchLongPress=*/true),
        epub(epub),
        epubPath(epubPath) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  void onRowLongPress(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  void onBackButton() override;
  const char* headerTitle() const override;
  void drawFooter() override;
  bool drawOverlay() override;
};
