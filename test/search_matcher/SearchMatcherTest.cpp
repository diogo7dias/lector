#include <Utf8.h>
#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "SearchMatcher.h"

namespace {
using Hits = std::vector<std::pair<uint32_t, std::string>>;

// Feeds `text` as one chapter, offsets counted per codepoint like the reader's parser.
Hits run(const char* query, const std::string& text) {
  SearchMatcher m;
  EXPECT_TRUE(m.setQuery(query));
  m.reset();
  Hits hits;
  auto onHit = [&](uint32_t off, const std::string& snip) { hits.emplace_back(off, snip); };
  const auto* p = reinterpret_cast<const unsigned char*>(text.c_str());
  uint32_t offset = 0;
  while (*p) m.feed(utf8NextCodepoint(&p), offset++, onHit);
  m.finish(onHit);
  return hits;
}
}  // namespace

TEST(SearchMatcher, FindsEveryHitAtTheOffsetOfItsFirstCodepoint) {
  const auto hits = run("cat", "The cat sat on the other cat.");
  ASSERT_EQ(2u, hits.size());
  EXPECT_EQ(4u, hits[0].first);
  EXPECT_EQ(25u, hits[1].first);
}

TEST(SearchMatcher, FoldsCaseQuotesAndWhitespace) {
  const auto hits = run("it's a  Dog", "Well\n  IT\xE2\x80\x99S\ta dog, said \xC3\x89mile.");  // It’S, É
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ(7u, hits[0].first);                                    // the I, after "Well\n  "
  EXPECT_EQ(1u, run("\xC3\xA9mile", "said \xC3\x89mile").size());  // é finds É
}

TEST(SearchMatcher, SnippetCarriesContextAndCollapsedSpaces) {
  const auto hits = run("needle", "hay   hay\nneedle hay");
  ASSERT_EQ(1u, hits.size());
  EXPECT_EQ("hay hay needle hay", hits[0].second);
}

TEST(SearchMatcher, LongContextIsTrimmedWithEllipses) {
  const std::string before(100, 'a');
  const std::string after(100, 'b');
  const auto hits = run("x", before + " x " + after);
  ASSERT_EQ(1u, hits.size());
  const std::string& s = hits[0].second;
  EXPECT_EQ(0u, s.rfind("…", 0));
  EXPECT_NE(std::string::npos, s.find(" x "));
  EXPECT_EQ(s.size() - 3, s.rfind("…"));
}

TEST(SearchMatcher, ChaptersDoNotRunTogether) {
  SearchMatcher m;
  ASSERT_TRUE(m.setQuery("ab"));
  Hits hits;
  auto onHit = [&](uint32_t off, const std::string& snip) { hits.emplace_back(off, snip); };
  m.reset();
  m.feed('a', 0, onHit);
  m.finish(onHit);
  m.reset();  // next chapter
  m.feed('b', 0, onHit);
  m.finish(onHit);
  EXPECT_TRUE(hits.empty());
}

TEST(SearchMatcher, RejectsEmptyAndOverlongQueries) {
  SearchMatcher m;
  EXPECT_FALSE(m.setQuery("   "));
  EXPECT_FALSE(m.setQuery(std::string(SearchMatcher::MAX_QUERY + 1, 'q').c_str()));
  EXPECT_TRUE(m.setQuery(std::string(SearchMatcher::MAX_QUERY, 'q').c_str()));
}
