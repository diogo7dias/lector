#include "WallpaperMoveActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>
#include <string>

#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "sleep/SdSleepImageFs.h"
#include "sleep/SleepImageMove.h"
#include "sleep/SleepPauseToggle.h"
#include "sleep/SleepWallpaperIndexStore.h"
#include "util/TaskWatchdog.h"

namespace {

using crosspoint::sleep::kSleepDir;
using crosspoint::sleep::kSleepPauseDir;

// Counting stops here so the confirmation appears promptly even on a folder of
// thousands; past this the prompt says "or more".
constexpr size_t COUNT_SCAN_CAP = 999;
// Names held at once during a move. 32 filenames is a few KB — small enough to
// be safe on a fragmented heap, large enough that a big folder does not cost a
// directory scan per file.
constexpr size_t MOVE_BATCH_SIZE = 32;
// Renames are individually quick but there can be thousands of them.
constexpr size_t MOVE_YIELD_EVERY = 16;

void feedWatchdog() {
  resetTaskWatchdogIfSubscribed();
  yield();
}

}  // namespace

StrId WallpaperMoveActivity::titleId() const {
  switch (job) {
    case Job::PauseFavorites:
      return StrId::STR_PAUSE_FAVORITE_WALLPAPERS;
    case Job::PauseOthers:
      return StrId::STR_PAUSE_OTHER_WALLPAPERS;
    case Job::RestoreAllPaused:
      break;
  }
  return StrId::STR_RESTORE_PAUSED_WALLPAPERS;
}

void WallpaperMoveActivity::onEnter() {
  Activity::onEnter();

  state = COUNTING;
  // The count is a full directory scan, which blocks this task, so put the
  // "counting" frame on the panel before starting it.
  requestUpdateAndWait();
  countMatches();

  if (matchCount == 0) {
    state = NOTHING_TO_DO;
    requestUpdate();
    return;
  }

  state = WARNING;
  char prompt[64];
  snprintf(prompt, sizeof(prompt),
           countCapped ? tr(STR_WALLPAPER_MOVE_CONFIRM_MANY_FORMAT) : tr(STR_WALLPAPER_MOVE_CONFIRM_FORMAT),
           static_cast<unsigned>(matchCount));
  const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
  confirmPopup.show(prompt, options, 2, 0, [this](int idx) {
    if (idx == 1) {
      beginMove();
    } else {
      goBack();
    }
  });
  requestUpdate();
}

void WallpaperMoveActivity::onExit() { Activity::onExit(); }

void WallpaperMoveActivity::countMatches() {
  crosspoint::sleep::SdSleepImageFs fs;
  if (job == Job::RestoreAllPaused) {
    // No favorite filter for a restore: everything paused goes back. Counting
    // both groups is one scan each, which is the same cost as a combined pass.
    matchCount = crosspoint::sleep::countImagesByFavorite(fs, kSleepPauseDir, true, COUNT_SCAN_CAP) +
                 crosspoint::sleep::countImagesByFavorite(fs, kSleepPauseDir, false, COUNT_SCAN_CAP);
  } else {
    matchCount = crosspoint::sleep::countImagesByFavorite(fs, kSleepDir, job == Job::PauseFavorites, COUNT_SCAN_CAP);
  }
  countCapped = matchCount >= COUNT_SCAN_CAP;
  LOG_INF("WPMOVE", "job %d matches %u%s", static_cast<int>(job), static_cast<unsigned>(matchCount),
          countCapped ? "+" : "");
}

void WallpaperMoveActivity::beginMove() {
  {
    RenderLock lock(*this);
    state = MOVING;
  }
  // The move blocks this task for as long as it runs, so the "moving" frame has
  // to reach the panel before it starts.
  requestUpdateAndWait();
  runMove();
}

void WallpaperMoveActivity::runMove() {
  crosspoint::sleep::SdSleepImageFs fs;
  crosspoint::sleep::MoveReport report;

  if (job == Job::RestoreAllPaused) {
    // Two filtered runs rather than an unfiltered one, so this reuses exactly the
    // same tested batching path as the pause jobs.
    const auto favorites = crosspoint::sleep::moveImagesByFavorite(
        fs, kSleepPauseDir, kSleepDir, /*moveFavorites=*/true, MOVE_BATCH_SIZE, MOVE_YIELD_EVERY, &feedWatchdog);
    const auto others = crosspoint::sleep::moveImagesByFavorite(fs, kSleepPauseDir, kSleepDir, /*moveFavorites=*/false,
                                                                MOVE_BATCH_SIZE, MOVE_YIELD_EVERY, &feedWatchdog);
    report.moved = favorites.moved + others.moved;
    report.failed = favorites.failed + others.failed;
    report.stalled = favorites.stalled || others.stalled;
  } else {
    report = crosspoint::sleep::moveImagesByFavorite(fs, kSleepDir, kSleepPauseDir, job == Job::PauseFavorites,
                                                     MOVE_BATCH_SIZE, MOVE_YIELD_EVERY, &feedWatchdog);
  }

  movedCount = report.moved;
  failedCount = report.failed;
  stalled = report.stalled;
  LOG_INF("WPMOVE", "moved %u, failed %u%s", static_cast<unsigned>(movedCount), static_cast<unsigned>(failedCount),
          stalled ? ", stalled" : "");
  // One mark per batch, not per file: the bulk move changed the sleep folder,
  // so the next cold boot reconciles the wallpaper index against it.
  if (report.moved > 0) crosspoint::sleep::windex::markDirty();

  state = DONE;
  requestUpdate();
}

void WallpaperMoveActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // Not tr(): the macro pastes its argument after "StrId::", so it only takes a
  // literal enumerator name, never a computed StrId.
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, I18N.get(titleId()));

  if (state == COUNTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_LOADING_POPUP));
    renderer.displayBuffer();
    return;
  }

  if (state == WARNING) {
    // No body text: the popup carries the question and the count, and anything
    // behind it would only show in the frame before the popup mounts.
    if (confirmPopup.processRender(renderer, mappedInput)) return;

    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), tr(STR_CONFIRM), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  if (state == MOVING) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_MOVING_WALLPAPERS));
    renderer.displayBuffer();
    return;
  }

  if (state == NOTHING_TO_DO) {
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2, tr(STR_WALLPAPER_NONE_TO_MOVE));
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // DONE
  char summary[64];
  snprintf(summary, sizeof(summary), tr(STR_WALLPAPER_MOVED_FORMAT), static_cast<unsigned>(movedCount));
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 - 20, summary, true, EpdFontFamily::REGULAR);
  if (stalled || failedCount > 0) {
    char stuck[64];
    snprintf(stuck, sizeof(stuck), tr(STR_WALLPAPER_MOVE_STALLED_FORMAT), static_cast<unsigned>(failedCount));
    renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 10, stuck);
  }
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

void WallpaperMoveActivity::loop() {
  if (state == WARNING) {
    if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) goBack();
    return;
  }

  if (state == DONE || state == NOTHING_TO_DO) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) goBack();
    return;
  }
}
