#include <Epub/TocHrefMatch.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// Runs the streaming matcher over a whole spine, as BookMetadataCache does.
int resolve(const std::string& tocHref, const std::vector<std::string>& spineHrefs) {
  TocHrefMatch matcher(tocHref);
  for (size_t i = 0; i < spineHrefs.size(); i++) {
    matcher.consider(static_cast<int16_t>(i), spineHrefs[i]);
  }
  return matcher.result();
}

TEST(TocHrefMatch, PrefersAnExactPathMatch) {
  EXPECT_EQ(resolve("OEBPS/Text/ch2.xhtml", {"OEBPS/Text/ch1.xhtml", "OEBPS/Text/ch2.xhtml"}), 1);
}

TEST(TocHrefMatch, MatchesOnFileNameWhenTheDirectoriesDiffer) {
  // A TOC document that lives in another folder resolves its hrefs against itself, so the
  // path built from it can name the right file through a different directory prefix.
  EXPECT_EQ(resolve("ch2.xhtml", {"OEBPS/Text/ch1.xhtml", "OEBPS/Text/ch2.xhtml"}), 1);
  EXPECT_EQ(resolve("OEBPS/./ch2.xhtml", {"OEBPS/ch1.xhtml", "OEBPS/ch2.xhtml"}), 1);
}

TEST(TocHrefMatch, RefusesAnAmbiguousFileNameMatch) {
  // Same file name in two folders: guessing would send the reader to the wrong chapter.
  EXPECT_EQ(resolve("index.html", {"OEBPS/part1/index.html", "OEBPS/part2/index.html"}), -1);
}

TEST(TocHrefMatch, ReportsNoMatchWhenTheTargetIsNotInTheSpine) {
  EXPECT_EQ(resolve("OEBPS/c0.xhtml", {"OEBPS/ch1.xhtml", "OEBPS/ch2.xhtml"}), -1);
  EXPECT_EQ(resolve("", {"OEBPS/ch1.xhtml"}), -1);
}

TEST(TocHrefMatch, AnExactMatchOutranksAnEarlierFileNameMatch) {
  EXPECT_EQ(resolve("OEBPS/Text/ch1.xhtml", {"OEBPS/other/ch1.xhtml", "OEBPS/Text/ch1.xhtml"}), 1);
}

}  // namespace
