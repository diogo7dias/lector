#include <gtest/gtest.h>

#include <string>

#include "util/ReadingJournal.h"

using namespace reading_journal;

TEST(ReadingJournal, ClockStopsOnAPageLeftOpen) {
  SessionClock c;
  c.touch(1000);                         // open
  c.touch(31000);                        // 30 s
  c.touch(31000 + IDLE_CUT_MS);          // exactly the cut still counts
  c.touch(31000 + 2 * IDLE_CUT_MS + 1);  // over it: walked away, adds nothing
  c.touch(31000 + 2 * IDLE_CUT_MS + 10001);
  EXPECT_EQ(30 + IDLE_CUT_MS / 1000 + 10, c.seconds());
}

TEST(ReadingJournal, ClockSurvivesMillisWrap) {
  SessionClock c;
  c.touch(UINT32_MAX - 4999);
  c.touch(5000);
  EXPECT_EQ(10u, c.seconds());
}

TEST(ReadingJournal, LineRoundTrips) {
  char buf[MAX_LINE];
  const Civil when{2026, 10, 18, 14, 5};
  const size_t n = formatLine(buf, sizeof(buf), &when, 1234, 56, true, "War\tand\nPeace");
  EXPECT_STREQ("2026-10-18T14:05\t1234\t56\t1\tWar and Peace\n", buf);
  EXPECT_EQ(strlen(buf), n);

  Entry e;
  ASSERT_TRUE(parseLine(std::string_view(buf, n - 1), e));
  EXPECT_TRUE(e.dated);
  EXPECT_EQ(2026, e.date.year);
  EXPECT_EQ(18, e.date.day);
  EXPECT_EQ(1234u, e.seconds);
  EXPECT_EQ(56u, e.pages);
  EXPECT_TRUE(e.done);
}

TEST(ReadingJournal, UndatedLineAndLongTitle) {
  char buf[48];
  const std::string title(100, 'x');
  const size_t n = formatLine(buf, sizeof(buf), nullptr, 60, 3, false, title.c_str());
  ASSERT_GT(n, 0u);
  EXPECT_EQ('\n', buf[n - 1]);
  EXPECT_EQ(0, strncmp(buf, "-\t60\t3\t0\txxx", 12));
  Entry e;
  ASSERT_TRUE(parseLine(std::string_view(buf, n - 1), e));
  EXPECT_FALSE(e.dated);
}

TEST(ReadingJournal, BadLinesAreSkipped) {
  Entry e;
  EXPECT_FALSE(parseLine("", e));
  EXPECT_FALSE(parseLine("2026-10-18T14:05\t12", e));           // cut off mid-append
  EXPECT_FALSE(parseLine("2026-10-18T14:05\t12\t3\t1", e));     // no title field
  EXPECT_FALSE(parseLine("2026-13-18T14:05\t12\t3\t1\tx", e));  // month 13
  EXPECT_FALSE(parseLine("2026-10-18 14:05\t12\t3\t1\tx", e));
  EXPECT_FALSE(parseLine("-\t-12\t3\t1\tx", e));
  EXPECT_FALSE(parseLine("-\t12\t3\t2\tx", e));
  EXPECT_TRUE(parseLine("-\t12\t3\t0\t\r", e));  // empty title, CRLF from a PC edit
}

TEST(ReadingJournal, SummaryWindows) {
  Summary s;
  s.setToday(Civil{2026, 3, 2, 9, 0});
  const char text[] =
      "2026-03-02T08:00\t600\t10\t0\ta\n"  // today
      "2026-02-24T08:00\t60\t1\t1\tb\n"    // 6 days back: in the week (2026 not leap)
      "2026-02-23T08:00\t120\t2\t0\tc\n"   // 7 days back: out of it
      "2025-12-31T23:59\t300\t5\t1\td\n"   // last year
      "-\t30\t1\t0\te\n"                   // undated: totals only
      "garbage line\n"
      "2026-03-02T09:00\t61\t1\t0\tf\n"
      "2026-03-05T09:00\t60\t1\t0\tg\n"  // after today: a clock set wrong, no square
      "2026-03-0";                       // partial tail, carried by the caller
  const size_t carry = s.addText(text, sizeof(text) - 1);
  EXPECT_EQ(9u, carry);
  EXPECT_EQ(7u, s.all.sessions);
  EXPECT_EQ(0, s.monthMinutes[4]);
  EXPECT_EQ(3u, s.week.sessions);
  EXPECT_EQ(721u, s.week.seconds);
  EXPECT_EQ(5u, s.year.sessions);
  EXPECT_EQ(1u, s.year.finished);
  EXPECT_EQ(2u, s.all.finished);
  EXPECT_EQ(12, s.monthMinutes[1]);  // 600 s + 61 s round up per session: 10 + 2
  EXPECT_EQ(0, s.monthMinutes[0]);
}

TEST(ReadingJournal, NoClockMeansTotalsOnly) {
  Summary s;
  const char text[] = "2026-03-02T08:00\t600\t10\t1\ta\n";
  s.addText(text, sizeof(text) - 1);
  EXPECT_EQ(1u, s.all.sessions);
  EXPECT_EQ(0u, s.week.sessions);
}

TEST(ReadingJournal, LocalTimeCrossesMidnightAndYear) {
  const int64_t utc = clock_zones::utcSeconds(2026, 12, 31, 23, 30);
  const Civil plus = localTime(utc, 60);
  EXPECT_EQ(2027, plus.year);
  EXPECT_EQ(1, plus.month);
  EXPECT_EQ(1, plus.day);
  EXPECT_EQ(0, plus.hour);
  EXPECT_EQ(30, plus.minute);
  const Civil minus = localTime(clock_zones::utcSeconds(2026, 3, 1, 0, 10), -30);
  EXPECT_EQ(2, minus.month);
  EXPECT_EQ(28, minus.day);
  EXPECT_EQ(23, minus.hour);
  EXPECT_EQ(40, minus.minute);
}

TEST(ReadingJournal, DurationAndMonthLength) {
  char buf[16];
  formatDuration(buf, sizeof(buf), 59);
  EXPECT_STREQ("0m", buf);
  formatDuration(buf, sizeof(buf), 45 * 60);
  EXPECT_STREQ("45m", buf);
  formatDuration(buf, sizeof(buf), 185 * 60);
  EXPECT_STREQ("3h05", buf);
  EXPECT_EQ(29, daysInMonth(2028, 2));
  EXPECT_EQ(28, daysInMonth(2100, 2));
  EXPECT_EQ(31, daysInMonth(2026, 12));
}
