// The two-pass replay of a side key's held-back edges.
//
// While the bindings router is deciding which gesture a key was, that key's raw
// edges are hidden from the rest of the firmware. When the router rules the
// gesture to be the paging the key already does, the held-back edges are handed
// over after the fact: the PRESS on the pass of the verdict, the RELEASE on the
// pass after.
//
// Getting this wrong is invisible in a unit of arithmetic and very visible on the
// device: drop the press and the page never turns; drop the release and a
// release-stepping list never finishes its step; put both on one pass and the
// page turns twice.
//
// This lived in main.cpp as a `static bool[2]` inside the loop, where nothing
// could reach it.
#include <gtest/gtest.h>

#include "util/ButtonReplay.h"

namespace {
constexpr bool kIntercepted = true;
constexpr bool kNotIntercepted = false;
constexpr bool kReplayNow = true;
constexpr bool kNoVerdict = false;
}  // namespace

TEST(ButtonReplay, AnIdleInterceptedKeyInjectsNothing) {
  button_replay::SideKey key;
  for (int pass = 0; pass < 5; ++pass) {
    const auto injection = key.onPass(kIntercepted, kNoVerdict);
    EXPECT_FALSE(injection.press);
    EXPECT_FALSE(injection.release);
  }
}

TEST(ButtonReplay, TheVerdictPassInjectsThePressAndTheNextPassTheRelease) {
  button_replay::SideKey key;

  const auto verdict = key.onPass(kIntercepted, kReplayNow);
  EXPECT_TRUE(verdict.press);
  EXPECT_FALSE(verdict.release) << "press and release must not share a pass: the consumer would move twice";

  const auto after = key.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(after.press);
  EXPECT_TRUE(after.release) << "the release is owed on the pass after the verdict";
}

TEST(ButtonReplay, TheReleaseIsOwedExactlyOnce) {
  button_replay::SideKey key;
  key.onPass(kIntercepted, kReplayNow);
  key.onPass(kIntercepted, kNoVerdict);  // release delivered here

  const auto third = key.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(third.release) << "the release repeated on a later pass: the list would step again";
  EXPECT_FALSE(key.releasePending());
}

TEST(ButtonReplay, BackToBackVerdictsKeepOnePressAndOneReleaseEachPass) {
  // A second gesture ruled on the very next pass: that pass owes the previous
  // release AND injects the new press. Both are correct together — they belong to
  // two different gestures.
  button_replay::SideKey key;

  const auto first = key.onPass(kIntercepted, kReplayNow);
  EXPECT_TRUE(first.press);
  EXPECT_FALSE(first.release);

  const auto second = key.onPass(kIntercepted, kReplayNow);
  EXPECT_TRUE(second.press) << "the new gesture's press";
  EXPECT_TRUE(second.release) << "the previous gesture's release";

  const auto third = key.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(third.press);
  EXPECT_TRUE(third.release);

  const auto fourth = key.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(fourth.release);
}

TEST(ButtonReplay, AKeyThatStopsBeingInterceptedDropsTheOwedRelease) {
  // Binding changed, or the context did (entering/leaving a book re-arms the
  // router and drops any gesture in flight). The key's own edges flow again from
  // here, so an injected release would be a second one.
  button_replay::SideKey key;
  key.onPass(kIntercepted, kReplayNow);
  ASSERT_TRUE(key.releasePending());

  const auto released = key.onPass(kNotIntercepted, kNoVerdict);
  EXPECT_FALSE(released.press);
  EXPECT_FALSE(released.release);
  EXPECT_FALSE(key.releasePending());

  // And it does not reappear when the key is intercepted again.
  const auto back = key.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(back.release);
}

TEST(ButtonReplay, AnUninterceptedKeyNeverInjects) {
  button_replay::SideKey key;
  for (int pass = 0; pass < 3; ++pass) {
    const auto injection = key.onPass(kNotIntercepted, kReplayNow);
    EXPECT_FALSE(injection.press) << "a key the router leaves alone holds nothing back, so has nothing to replay";
    EXPECT_FALSE(injection.release);
  }
}

TEST(ButtonReplay, ResetClearsAnOwedRelease) {
  button_replay::SideKey key;
  key.onPass(kIntercepted, kReplayNow);
  ASSERT_TRUE(key.releasePending());
  key.reset();
  EXPECT_FALSE(key.releasePending());
  EXPECT_FALSE(key.onPass(kIntercepted, kNoVerdict).release);
}

TEST(ButtonReplay, TheTwoSideKeysDoNotShareState) {
  button_replay::SideKey upper;
  button_replay::SideKey lower;

  upper.onPass(kIntercepted, kReplayNow);
  const auto lowerPass = lower.onPass(kIntercepted, kNoVerdict);
  EXPECT_FALSE(lowerPass.release) << "one key's owed release leaked into the other";
  EXPECT_TRUE(upper.releasePending());
}
