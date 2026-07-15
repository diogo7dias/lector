#pragma once
#include <functional>
#include <string>
#include <vector>

#include "./FileBrowserActivity.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct RecentBook;
struct Rect;

class HomeActivity final : public Activity {
  ButtonNavigator buttonNavigator;
  int selectorIndex = 0;
  // SINGLE_COVER (coverflow) home: which recent book is centered. Switched with the
  // side / page-turn buttons. Independent of selectorIndex (which picks pages tile /
  // cover slot / menu row).
  int coverflowIndex = 0;
  // List-home (HOME_LAYOUT_LIST) scroll state: scrollOffset is the topmost book
  // index the renderer may start from; first/lastVisibleBookIdx are fed back by
  // drawRecentBookList so loop() can keep the selected row on-screen.
  int scrollOffset = 0;
  int firstVisibleBookIdx = 0;
  int lastVisibleBookIdx = 0;
  bool recentsLoaded = false;
  // Next recentBooks index the cover-thumb pump will examine (see
  // pumpRecentCovers: one thumb generated per loop() pass).
  size_t coverGenIndex = 0;
  bool firstRenderDone = false;
  // First paint after (re)entering Home does a FULL refresh to clear e-ink
  // ghosting left by the reader/menus; in-Home up/down nav stays on FAST_REFRESH
  // so it remains snappy. Set on every onEnter, cleared after the first paint.
  bool pendingFullRefresh = true;
  bool hasOpdsServers = false;
  // Wake press meter (wakeDiagnostics): millis() when the entry paint's
  // refresh returned (menu physically on screen and loop back in control),
  // and a one-shot guard for the first-press latency band.
  unsigned long wakeMenuReadyAt_ = 0;
  bool wakePressMeterDone_ = false;
  bool coverRendered = false;      // Track if cover has been rendered once
  bool coverBufferStored = false;  // Track if cover buffer is stored
  uint8_t* coverBuffer = nullptr;  // HomeActivity's own buffer for cover image
  size_t coverBufferSize = 0;      // Bytes allocated to coverBuffer
  // Logical rect last passed to drawRecentBookCover. The cover snapshot only
  // needs to cover this region, not the entire framebuffer, so we cache the
  // tile instead of all 48 KB. Set in render() before the call.
  int coverRectX = 0;
  int coverRectY = 0;
  int coverRectW = 0;
  int coverRectH = 0;
  std::vector<RecentBook> recentBooks;
  const HomeMenuItem initialMenuItem;

  // Sleep-folder overflow. When /sleep reaches kSleepIndexMaxImages (the index
  // rotation engine's ceiling — thousands are fine below it) the LIST home shows
  // a selectable warning card (selector slot 1, pushing books down) that opens a
  // keypad to randomly move N wallpapers to /sleep pause. A separate one-shot
  // toast reports wallpapers a legacy reconcile auto-moved while asleep.
  // The count comes from APP_STATE.sleepIndexCount (the persisted sleep-index
  // snapshot, refreshed by the windex idle pump) — home entry does NO directory
  // walk. Every wake is a cold boot, so the old first-entry countImages(5000)
  // walk sat on the wake-to-menu path for nothing: capped at 5000, it could
  // never reach the 20000 warning threshold.
  bool sleepOverLimit = false;
  long sleepImageCount = 0;
  std::string sleepPauseToast;  // "N moved to /sleep pause/" — shown once on entry
  std::string moveToast;        // "Moved N" — result popup after a bulk move
  // The over-limit warning card is a LIST-home-only slot; it sits at selector 1,
  // between the pages tally (0) and the first book.
  int listWarnSlots() const { return sleepOverLimit ? 1 : 0; }
  int firstBookList() const { return 1 + listWarnSlots(); }
  void refreshSleepOverLimit();
  void maybeShowWallpaperPauseToast();
  void openSleepMoveKeypad();
  // Draw (once) any pending sleep-overflow popup over the just-rendered home frame.
  void drawSleepToasts();

  // Convert HomeMenuItem to menu index (used in onEnter). Recent Books no longer
  // lives on the home menu — it is reached from the top of the file browser — so
  // it is intentionally absent here; a RECENTS request falls through to index 0.
  static int menuItemToIndex(HomeMenuItem item, bool hasOpdsUrl) {
    int i = 0;
    if (item == HomeMenuItem::FILE_BROWSER) return i;
    ++i;
    if (item == HomeMenuItem::OPDS_BROWSER) return hasOpdsUrl ? i : 0;
    if (hasOpdsUrl) ++i;
    if (item == HomeMenuItem::FILE_TRANSFER) return i;
    ++i;
    if (item == HomeMenuItem::SETTINGS_MENU) return i;
    return 0;
  }

  // Convert menu index to HomeMenuItem (used in loop)
  static HomeMenuItem indexToMenuItem(int idx, bool hasOpdsUrl) {
    int i = 0;
    if (idx == i++) return HomeMenuItem::FILE_BROWSER;
    if (hasOpdsUrl && idx == i++) return HomeMenuItem::OPDS_BROWSER;
    if (idx == i++) return HomeMenuItem::FILE_TRANSFER;
    if (idx == i) return HomeMenuItem::SETTINGS_MENU;
    return HomeMenuItem::NONE;
  }
  void onSelectBook(const std::string& path);
  void promptRemoveBook(const std::string& path, const std::string& title);
  void onFileBrowserOpen();
  void onSettingsOpen();
  void onFileTransferOpen();
  void onOpdsBrowserOpen();

  int getMenuItemCount() const;
  // Shared home top line (version label + Pages tally pill + clock + battery header),
  // drawn identically by both the LIST and SINGLE_COVER layouts.
  void drawHomeTopLine(int pageWidth, bool pagesSelected);
  bool storeCoverBuffer();    // Store frame buffer for cover image
  bool restoreCoverBuffer();  // Restore frame buffer from stored cover
  void freeCoverBuffer();     // Free the stored cover buffer
  void loadRecentBooks(int maxBooks);
  void pumpRecentCovers(int coverHeight);

 public:
  explicit HomeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                        HomeMenuItem initialMenuItemValue = HomeMenuItem::NONE)
      : Activity("Home", renderer, mappedInput), initialMenuItem(initialMenuItemValue) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
