#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "RecentBooksStore.h"
#include "SdFileIndex.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class FileBrowserActivity final : public Activity {
 public:
  // Books = standard reader browser; PickFirmware = filter to .bin only and return path via ActivityResult.
  enum class Mode { Books, PickFirmware };

 private:
  // Deletion
  bool removeDirFile(const std::string& fullPath);

  ButtonNavigator buttonNavigator;

  size_t selectorIndex = 0;

  bool lockLongPressBack = false;
  // True when this activity was entered while Confirm was already held; we must swallow the next
  // release so we don't immediately auto-open the first entry.
  bool lockNextConfirmRelease = false;

  // Forces the NEXT paint onto a FULL_REFRESH instead of the default FAST partial
  // refresh, then auto-clears. Set it after events that leave e-ink residue a fast
  // refresh cannot scrub — chiefly returning from the full-page grayscale PXC viewer
  // (its image ghosts behind the list), plus deletes and folder entry. Plain scrolling
  // stays on FAST_REFRESH so page-through latency is unchanged. Mirrors HomeActivity.
  bool pendingFullRefresh = true;

  Mode mode = Mode::Books;

  // Files state
  std::string basepath = "/";
  std::vector<std::string> files;
  std::unique_ptr<char[]> fileNameBuffer;
  bool sdMode = false;
  SdFileIndex sdIndex;

  size_t fileCount() const { return sdMode ? sdIndex.count() : files.size(); }
  std::string fileNameAt(size_t index) const {
    if (sdMode) return sdIndex.nameAt(index);
    return index < files.size() ? files[index] : std::string{};
  }

  // ── Fuzzy search (ported from DX34) ──────────────────────────────────────
  // Active query for the current folder; empty = no search. filteredIndexes holds
  // the ranked indices into `files` while a search is active. folderHasBooks_ gates
  // whether the "Search current folder" row is offered (Books mode, folder has a book).
  std::string activeSearchQuery;
  std::vector<size_t> filteredIndexes;
  bool folderHasBooks_ = false;

  bool searchActive() const { return !activeSearchQuery.empty(); }
  int searchRowCount() const { return folderHasBooks_ ? 1 : 0; }
  int clearRowCount() const { return searchActive() ? 1 : 0; }
  // Synthetic rows pinned above the file rows: Recent Books + Search + Clear.
  int headerRowCount() const { return (hasRecentShortcut() ? 1 : 0) + searchRowCount() + clearRowCount(); }
  size_t fileRowCount() const { return searchActive() ? filteredIndexes.size() : fileCount(); }
  size_t totalRowCount() const { return static_cast<size_t>(headerRowCount()) + fileRowCount(); }

  enum class RowKind { Recent, Search, Clear, File };
  struct RowRef {
    RowKind kind;
    size_t fileIndex = 0;  // valid when kind == File; index into `files`
  };
  RowRef rowAt(size_t rowIndex) const;

  void openSearch();
  void rebuildFilter();
  void clearSearch();

  // Data loading
  void loadFiles();
  size_t findEntry(const std::string& name) const;

  // Open a .pxc wallpaper in the viewer without tearing down this browser: launched
  // via startActivityForResult so we stay alive with `files` resident, then patch the
  // one affected row in place on return (moved/deleted -> erased; favorite-renamed ->
  // updated) instead of re-scanning the whole folder. The launch identity is the fast
  // path; a BMP sibling move resolves the changed sibling by its source name.
  void openPxcViewer(const std::string& path, const std::string& launchName, size_t launchFileIndex);
  void openBmpViewer(const std::string& path, const std::string& launchName, size_t launchFileIndex);
  ActivityResultHandler imageViewerResultHandler(const std::string& launchName, size_t launchFileIndex);

  // A "Recent Books" shortcut row is pinned to the top of the list only at the SD
  // root in the normal book browser (never in the firmware picker or subfolders).
  // When present it occupies selector index 0 and shifts the real files down by one.
  bool hasRecentShortcut() const { return mode == Mode::Books && basepath == "/"; }

 public:
  explicit FileBrowserActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::string initialPath = "/",
                               Mode mode = Mode::Books)
      : Activity("FileBrowser", renderer, mappedInput),
        mode(mode),
        basepath(initialPath.empty() ? "/" : std::move(initialPath)) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
