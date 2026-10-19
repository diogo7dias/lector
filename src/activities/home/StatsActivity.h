#pragma once

#include "activities/UiStatusActivity.h"
#include "util/ReadingJournal.h"

// Reading stats from /stats/journal.txt. The month leads: a Monday-first calendar where
// each day fills from the bottom by the time read that day. Under it (beside it in
// landscape) the last seven days, this year, all time and books finished. Read once on
// entry; nothing here changes while the screen is up.
class StatsActivity final : public UiStatusActivity {
  reading_journal::Summary summary;
  bool haveJournal = false;

  // The month (title, weekday letters, days) from x,y within width and maxHeight;
  // returns the height it used.
  int drawMonth(int x, int y, int width, int maxHeight) const;
  // One label / value row per total, from y down.
  void drawRows(int x, int y, int width) const;

 public:
  StatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("Stats", renderer, mappedInput) {}
  void onEnter() override;

 protected:
  StatusView statusView() const override;
  bool drawOverlay() override;
};
