#pragma once

#include <NameList.h>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "RecentBooksStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"
#include "util/HoldButtonPolicy.h"

class FileBrowserActivity final : public UiListActivity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via
  // ActivityResult; PickFolder = folders only, returning the folder the reader stops in, which
  // is how a book is moved into one.
  enum class Mode { Books, PickFirmware, PickFolder };

 private:
  // Deletion
  bool removeDirFile(const std::string& fullPath);
  /** Opens the folder picker and moves `fullPath` into whatever it comes back with. */
  void promptMoveToFolder(const std::string& fullPath);
  void onMoveDestinationResult(const std::string& fullPath, const ActivityResult& result);
  // Opens the delete confirmation for one entry.
  void confirmDelete(const std::string& fullPath);

  // Holding Confirm on a file opens this: send it to a nearby reader, or delete it.
  OptionPopup fileActionPopup;
  // What the list rows point into, rebuilt with them.
  std::vector<freeink::ui::ListItem> rows;
  std::vector<std::string> rowText;

  // Confirm and Back each carry a short and a hold action; the trackers decide which
  // fired and keep the hold from also counting as a short press on the way up.
  hold_button::Tracker confirmHold;
  hold_button::Tracker backHold;
  // Spend one FULL refresh on the next frame, then fall back to FAST. Set on entry
  // and on return from a child activity, since the panel may arrive here carrying
  // ghosts from screens that only ever paint FAST.
  bool pendingFullRefresh = true;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  // Arena-backed and bounded: a wallpaper folder with thousands of images used to
  // exhaust the heap here, and a throwing allocation aborts the firmware.
  NameList files;
  std::unique_ptr<char[]> fileNameBuffer;

  // In-folder search. `files` always holds the whole folder; when a search is running,
  // `filtered` holds the ranked subset of indices into it and the list draws through
  // that. The search rows are synthetic list rows above the entries, so the search is
  // reachable with the same two buttons as everything else.
  std::string searchQuery;
  std::vector<int> filtered;
  // Whether this folder holds anything to search. An all-folders folder gets no row.
  bool folderHasEntries = false;

  enum class RowKind { Search, ClearSearch, Entry };
  bool searchActive() const { return !searchQuery.empty(); }
  int headerRowCount() const;
  int entryRowCount() const;
  int totalRowCount() const;
  RowKind rowKindAt(int row) const;
  // Index into `files` for an Entry row; -1 for the synthetic rows.
  int fileIndexAt(int row) const;
  std::string rowTitle(int row) const;
  std::string rowValue(int row) const;
  // The reading progress under the title ("42%", or Read). Empty for a row that is not a
  // book or for a book that has never been opened.
  std::string rowBadge(int row) const;
  // Reading badge for one listing entry: the stored percent, or -1 for a book that was
  // never opened (and for anything that is not a book). Memoised per listing because the
  // value costs an SD read and every render pass asks the visible rows again.
  int readingPercentAt(int index) const;
  mutable std::vector<int16_t> readingPercents;
  // Holds the row label the batch prewarm getter is handing back: rowTitle()
  // returns a temporary, and the getter must return a pointer that stays valid
  // until the batch reads it (see prewarmRowGlyphs()).
  mutable std::string prewarmScratch;
  void prewarmRowGlyphs() const;
  void openSearchEntry();
  void applySearch(const std::string& query);
  void clearSearch();

  // Data loading
  void loadFiles();
  // Re-orders the files (never the folders) to match the browser order setting. Random
  // shuffles in place; the two date orders sort by `sortKeys`, gathered during the listing
  // scan. Alphabetical is the order the listing already arrives in and needs no work.
  void applyBrowserOrder();
  // True when the current setting needs a key per entry gathered while the folder is
  // scanned (the two date orders, books only).
  // Search belongs to whatever folder is on screen, so the light panel's Search button
  // is this screen's to answer. Anywhere else ActivityManager opens the browser instead.
  bool runBoundAction(uint8_t function) override {
    if (function != CrossPointSettings::LP_MENU_SEARCH) return false;
    openSearchEntry();
    return true;
  }

  // The light panel's Sort row changed the order under us. Re-read rather than re-sort:
  // Last Read reads a key per book off the card, and those keys are gathered by the scan.
  void onBookOrderChanged() override {
    RenderLock lock(*this);
    loadFiles();
    requestUpdate();
  }

  bool isWallpaperFolder() const;
  bool needsSortKeys() const;
  // Appends one key, growing the buffer through the nothrow path. False means the folder is
  // too big to key and the listing stays alphabetical.
  bool pushSortKey(uint32_t key);
  // Second pass for Last Read: replaces each book's placeholder key with the read counter
  // stamped beside its cache. One SD open per book, so it feeds the watchdog as it goes.
  void fillLastReadKeys();
  // One sort key per entry, in listing order, or empty when the order needs none. Held for
  // the length of a load only: the browser reads it once, in applyBrowserOrder().
  std::vector<uint32_t> sortKeys;
  size_t findEntryRow(const std::string& name) const;
  // Open, descend into or (holdAction) act on one row.
  void activateRow(int row, bool holdAction);

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : UiListActivity(activity_name::kFileBrowser, renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;

 protected:
  int listCount() const override { return totalRowCount(); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  bool handleButtons() override;
  ListChrome chrome() const override;
  HalDisplay::RefreshMode refreshMode() override;
  bool drawOverlay() override;
};
