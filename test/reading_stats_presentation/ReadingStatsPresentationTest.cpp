#include <gtest/gtest.h>

#include "ReadingStatsPresentation.h"

namespace {

using reading_stats::ReadingStatsData;

// The X4 has no RTC, so a session read on it fills the counters and nothing else: the
// stats screen uses this to say "clock not set" instead of drawing dashes and empty
// charts that look like a broken screen.
TEST(HasDatedActivity, FalseWhenOnlyCountersWereRecorded) {
  ReadingStatsData stats;
  stats.totalSessions = 4;
  stats.totalReadingSeconds = 3600;
  stats.totalPagesTurned = 120;

  EXPECT_FALSE(reading_stats::hasDatedActivity(stats));
}

TEST(HasDatedActivity, TrueOnAStartDay) {
  ReadingStatsData stats;
  stats.startDay = 20000;

  EXPECT_TRUE(reading_stats::hasDatedActivity(stats));
}

TEST(HasDatedActivity, TrueOnAFinishedDay) {
  ReadingStatsData stats;
  stats.finishedDay = 20001;

  EXPECT_TRUE(reading_stats::hasDatedActivity(stats));
}

TEST(HasDatedActivity, TrueOnASingleSecondInAnyBucket) {
  for (size_t index = 0; index < reading_stats::kTimeOfDayBucketCount; index++) {
    ReadingStatsData stats;
    stats.timeOfDaySeconds[index] = 1;
    EXPECT_TRUE(reading_stats::hasDatedActivity(stats)) << "time of day bucket " << index;
  }
  for (size_t index = 0; index < reading_stats::kDayOfWeekCount; index++) {
    ReadingStatsData stats;
    stats.dayOfWeekSeconds[index] = 1;
    EXPECT_TRUE(reading_stats::hasDatedActivity(stats)) << "weekday bucket " << index;
  }
}

TEST(HasDatedActivity, FalseOnFreshStats) { EXPECT_FALSE(reading_stats::hasDatedActivity(ReadingStatsData{})); }

}  // namespace
