#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"

// Bulk-moves sleep wallpapers between /sleep and "/sleep pause", filtered by
// favorite state. Nothing moves until the user confirms a prompt that states how
// many files the run will touch, because on a folder of thousands this is not an
// action anyone should trigger blind.
//
// The move itself is SleepImageMove's bounded-pass algorithm, so a huge folder
// costs one batch of filenames of heap rather than the whole listing.
class WallpaperMoveActivity final : public Activity {
 public:
  enum class Job {
    PauseFavorites,   // /sleep -> "/sleep pause", favorites only
    PauseOthers,      // /sleep -> "/sleep pause", everything not a favorite
    RestoreAllPaused  // "/sleep pause" -> /sleep, everything
  };

  WallpaperMoveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Job job)
      : Activity("WallpaperMove", renderer, mappedInput), job(job) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
  void render(RenderLock&&) override;

 private:
  enum State { COUNTING, WARNING, MOVING, DONE, NOTHING_TO_DO };

  void goBack() { finish(); }
  StrId titleId() const;
  void countMatches();
  void beginMove();
  void runMove();

  Job job;
  State state = COUNTING;
  size_t matchCount = 0;
  // True when the count stopped at the scan cap, so the prompt says "or more"
  // rather than quoting a number that is not the whole story.
  bool countCapped = false;
  size_t movedCount = 0;
  size_t failedCount = 0;
  bool stalled = false;
  OptionPopup confirmPopup;
};
