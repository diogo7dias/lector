#pragma once

#include "activities/UiStatusActivity.h"
#include "util/ReadingJournal.h"

// Reading stats from /stats/journal.txt: the last seven days, this year, all time, and
// this month as a calendar with the days read filled in. Read once on entry; nothing
// here changes while the screen is up.
class StatsActivity final : public UiStatusActivity {
  reading_journal::Summary summary;
  bool haveJournal = false;
  char weekLine[48] = "";
  char yearLine[48] = "";
  char yearFinished[40] = "";
  char allLine[48] = "";
  char allFinished[40] = "";
  char daysLine[64] = "";

 public:
  StatsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiStatusActivity("Stats", renderer, mappedInput) {}
  void onEnter() override;

 protected:
  StatusView statusView() const override;
  bool drawOverlay() override;
};
