#include <gtest/gtest.h>

#include "activities/home/LibrarySearch.h"

using namespace librarysearch;

namespace {

Match scoreOf(const std::string& name, const std::string& query) {
  Match m;
  EXPECT_TRUE(scoreEntry(name, query, m)) << name << " should match " << query;
  return m;
}

bool matches(const std::string& name, const std::string& query) {
  Match m;
  return scoreEntry(name, query, m);
}

}  // namespace

TEST(ScoreEntry, EmptyQueryMatchesNothing) { EXPECT_FALSE(matches("Dune.epub", "")); }

TEST(ScoreEntry, EmptyNameMatchesNothing) { EXPECT_FALSE(matches("", "dune")); }

TEST(ScoreEntry, PrefixIsTheBestTier) { EXPECT_EQ(scoreOf("Dune.epub", "dun").tier, 0); }

TEST(ScoreEntry, MatchingIsCaseInsensitiveBothWays) {
  EXPECT_EQ(scoreOf("dune.epub", "DUN").tier, 0);
  EXPECT_EQ(scoreOf("DUNE.epub", "dun").tier, 0);
}

TEST(ScoreEntry, AWordInsideTheNameIsTierOne) {
  const Match m = scoreOf("The Great Gatsby.epub", "gatsby");
  EXPECT_EQ(m.tier, 1);
  EXPECT_EQ(m.score, 10);  // where the word starts
}

TEST(ScoreEntry, PunctuationAndUnderscoresStartWords) {
  EXPECT_EQ(scoreOf("the_great_gatsby.epub", "gatsby").tier, 1);
  EXPECT_EQ(scoreOf("brave-new-world.epub", "new").tier, 1);
}

TEST(ScoreEntry, LettersInOrderWithGapsAreTierTwo) {
  EXPECT_EQ(scoreOf("The Great Gatsby.epub", "gtg").tier, 2);
}

TEST(ScoreEntry, OutOfOrderLettersDoNotMatch) { EXPECT_FALSE(matches("The Great Gatsby.epub", "ytg")); }

TEST(ScoreEntry, ATrailingSlashOnAFolderIsIgnored) {
  EXPECT_EQ(scoreOf("Sci-Fi/", "sci").tier, 0);
  EXPECT_FALSE(matches("/", "s"));
}

TEST(ScoreEntry, ATighterTierTwoMatchScoresLower) {
  const Match tight = scoreOf("abc zzzzzzzz.epub", "abc");
  const Match loose = scoreOf("a b c zzzzzzzz.epub", "abc");
  EXPECT_LT(tight.score, loose.score);
}

TEST(RankMatches, EmptyQueryReturnsNothing) {
  const std::vector<std::string> names{"Dune.epub", "Neuromancer.epub"};
  EXPECT_TRUE(rankMatches(names, "").empty());
}

TEST(RankMatches, DropsEntriesThatDoNotMatch) {
  const std::vector<std::string> names{"Dune.epub", "Neuromancer.epub"};
  const std::vector<int> hits = rankMatches(names, "dune");
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0], 0);
}

TEST(RankMatches, BetterTiersComeFirst) {
  // "Gatsby Notes" is a prefix hit; "The Great Gatsby" is a word hit; "Galactic Sunset
  // by Night" holds g-a-t-s-b-y in order and only matches as scattered letters.
  const std::vector<std::string> names{"The Great Gatsby.epub", "Galactic Sunset by Night.epub", "Gatsby Notes.epub"};
  const std::vector<int> hits = rankMatches(names, "gatsby");
  ASSERT_EQ(hits.size(), 3u);
  EXPECT_EQ(hits[0], 2);  // prefix
  EXPECT_EQ(hits[1], 0);  // word start
  EXPECT_EQ(hits[2], 1);  // scattered
}

TEST(RankMatches, EqualMatchesKeepListingOrder) {
  const std::vector<std::string> names{"Dune 1.epub", "Dune 2.epub", "Dune 3.epub"};
  const std::vector<int> hits = rankMatches(names, "dune");
  ASSERT_EQ(hits.size(), 3u);
  EXPECT_EQ(hits[0], 0);
  EXPECT_EQ(hits[1], 1);
  EXPECT_EQ(hits[2], 2);
}

TEST(RankMatches, FoldersAreSearchableToo) {
  const std::vector<std::string> names{"Sci-Fi/", "Dune.epub"};
  const std::vector<int> hits = rankMatches(names, "sci");
  ASSERT_EQ(hits.size(), 1u);
  EXPECT_EQ(hits[0], 0);
}
