#include <Epub/SpineFileNameIndex.h>
#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

// Builds the index over a whole spine, as BookMetadataCache does on the first TOC entry
// the exact-path lookup cannot answer.
int resolve(const std::string& tocHref, const std::vector<std::string>& spineHrefs) {
  SpineFileNameIndex index;
  index.reserve(spineHrefs.size());
  for (size_t i = 0; i < spineHrefs.size(); i++) {
    index.add(static_cast<int16_t>(i), spineHrefs[i]);
  }
  index.seal();
  return index.resolve(tocHref);
}

TEST(SpineFileNameIndex, MatchesOnFileNameWhenTheDirectoriesDiffer) {
  // A TOC document that lives in another folder resolves its hrefs against itself, so the
  // path built from it can name the right file through a different directory prefix.
  EXPECT_EQ(resolve("ch2.xhtml", {"OEBPS/Text/ch1.xhtml", "OEBPS/Text/ch2.xhtml"}), 1);
  EXPECT_EQ(resolve("OEBPS/./ch2.xhtml", {"OEBPS/ch1.xhtml", "OEBPS/ch2.xhtml"}), 1);
}

TEST(SpineFileNameIndex, RefusesAnAmbiguousFileName) {
  // Same file name in two folders: guessing would send the reader to the wrong chapter.
  EXPECT_EQ(resolve("index.html", {"OEBPS/part1/index.html", "OEBPS/part2/index.html"}), -1);
  // Still refused when the duplicates do not sit next to each other in the spine.
  EXPECT_EQ(resolve("index.html", {"a/index.html", "a/ch1.xhtml", "b/index.html"}), -1);
}

TEST(SpineFileNameIndex, ReportsNoMatchWhenTheNameIsNotInTheSpine) {
  EXPECT_EQ(resolve("OEBPS/c0.xhtml", {"OEBPS/ch1.xhtml", "OEBPS/ch2.xhtml"}), -1);
  EXPECT_EQ(resolve("", {"OEBPS/ch1.xhtml"}), -1);
  EXPECT_EQ(resolve("OEBPS/", {"OEBPS/ch1.xhtml"}), -1);
}

TEST(SpineFileNameIndex, FindsTheOnlyCarrierAmongManyOtherNames) {
  std::vector<std::string> spine;
  spine.reserve(500);
  for (int i = 0; i < 500; i++) spine.push_back("OEBPS/Text/ch" + std::to_string(i) + ".xhtml");
  EXPECT_EQ(resolve("ch499.xhtml", spine), 499);
  EXPECT_EQ(resolve("Text/ch0.xhtml", spine), 0);
  EXPECT_EQ(resolve("ch500.xhtml", spine), -1);
}

TEST(SpineFileNameIndex, ClearMakesItReusable) {
  SpineFileNameIndex index;
  index.add(0, "a/one.xhtml");
  index.seal();
  EXPECT_TRUE(index.sealed());
  EXPECT_EQ(index.resolve("one.xhtml"), 0);

  index.clear();
  EXPECT_FALSE(index.sealed());
  index.add(0, "a/two.xhtml");
  index.seal();
  EXPECT_EQ(index.resolve("one.xhtml"), -1);
  EXPECT_EQ(index.resolve("two.xhtml"), 0);
}

}  // namespace

// Guards the constant itself: a wrong FNV offset basis still hashes consistently, so
// every test above would pass while the value silently stopped matching the exact-href
// index this is meant to sit beside.
TEST(SpineFileNameIndex, UsesTheStandardFnv1a64Constants) {
  // FNV-1a 64 of "a" with basis 14695981039346656037 and prime 1099511628211.
  EXPECT_EQ(SpineFileNameIndex::hashOf("a"), 12638187200555641996ULL);
  EXPECT_EQ(SpineFileNameIndex::hashOf(""), 14695981039346656037ULL);
}

// The index answers by file name only; the caller resolves the exact path first. A TOC
// href that IS the exact spine path still resolves here, because its file name is unique
// — that is what keeps the fallback safe to reach for an entry the exact lookup missed.
TEST(SpineFileNameIndex, AnExactPathResolvesWhenItsFileNameIsUnique) {
  EXPECT_EQ(resolve("OEBPS/Text/ch2.xhtml", {"OEBPS/Text/ch1.xhtml", "OEBPS/Text/ch2.xhtml"}), 1);
}

// ...but it refuses when the name is shared, even though one of the two IS the exact
// path. Exact-path precedence is the caller's job, so the fallback must not guess here.
TEST(SpineFileNameIndex, RefusesASharedNameEvenWhenOneCarrierIsTheExactPath) {
  EXPECT_EQ(resolve("OEBPS/Text/ch1.xhtml", {"OEBPS/other/ch1.xhtml", "OEBPS/Text/ch1.xhtml"}), -1);
}
