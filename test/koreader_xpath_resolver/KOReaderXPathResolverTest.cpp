#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "ChapterXPathResolver.h"

namespace {
Epub epubWith(std::string xhtml) {
  std::vector<std::string> spine;
  spine.push_back(std::move(xhtml));
  return Epub(std::move(spine));
}

constexpr char kNestedFixture[] = R"(<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body><div><section><p>Alpha bravo</p><p>Second <em>nested</em> tail</p></section></div></body></html>)";

constexpr char kNonVisibleInlineFixture[] =
    R"(<html><body><p><RP><span>hidden</span></RP>Visible text</p></body></html>)";

constexpr char kCommentBoundaryFixture[] = R"(<html><body><p>before<!--comment-->after</p></body></html>)";
constexpr char kProcessingInstructionBoundaryFixture[] = R"(<html><body><p>before<?marker?>after</p></body></html>)";
constexpr char kCdataBoundaryFixture[] = R"(<html><body><p>before<![CDATA[middle]]>after</p></body></html>)";
constexpr char kHiddenCdataFixture[] = R"(<html><body><p>before<rp><![CDATA[hidden]]></rp>after</p></body></html>)";
}  // namespace

TEST(KOReaderXPathResolver, ResolvesExactOffsetWithFullAncestry) {
  auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].6");
}

TEST(KOReaderXPathResolver, PreservesNestedInlineTextNode) {
  auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 26),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]/text()[2].2");
}

TEST(KOReaderXPathResolver, EmitsDetailedAnchorForOffsetZero) {
  auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, IgnoresNestedNonVisibleInlineText) {
  auto epub = epubWith(kNonVisibleInlineFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, ResolvesProgressAfterNestedNonVisibleInlineText) {
  auto epub = epubWith(kNonVisibleInlineFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForProgress(epub, 0, 1.0f), "/body/DocFragment[1]/body/p[1]/text()[1].12");
}

TEST(KOReaderXPathResolver, CountsUtf8CodepointsInsteadOfBytes) {
  auto epub = epubWith(
      "<?xml version=\"1.0\" encoding=\"UTF-8\"?><html><body><p>A\xC3\xA9\xE4\xB8\xAD"
      "B</p></body></html>");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 3),
            "/body/DocFragment[1]/body/p[1]/text()[1].3");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundComments) {
  auto epub = epubWith(kCommentBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 6),
            "/body/DocFragment[1]/body/p[1]/text()[2].0");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 11).empty());
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundProcessingInstructions) {
  auto epub = epubWith(kProcessingInstructionBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, SplitsTextNodesAroundCdata) {
  auto epub = epubWith(kCdataBoundaryFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 13),
            "/body/DocFragment[1]/body/p[1]/text()[3].1");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstComment) {
  auto epub = epubWith(R"(<html><body><p><!--comment-->text</p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstProcessingInstruction) {
  auto epub = epubWith(R"(<html><body><p><?marker?>text</p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, DoesNotCreateNodesBeforeFirstCdata) {
  auto epub = epubWith(R"(<html><body><p><![CDATA[text]]></p></body></html>)");

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(epub, 0, 0),
            "/body/DocFragment[1]/body/p[1]/text()[1].0");
}

TEST(KOReaderXPathResolver, CountsVisibleCdataAndIgnoresHiddenCdata) {
  auto visible = epubWith(kCdataBoundaryFixture);
  auto hidden = epubWith(kHiddenCdataFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(visible, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
  EXPECT_EQ(ChapterXPathResolver::findXPathForVisibleTextOffset(hidden, 0, 7),
            "/body/DocFragment[1]/body/p[1]/text()[2].1");
}

TEST(KOReaderXPathResolver, ReturnsEmptyForUnusableContent) {
  auto empty = epubWith("");
  auto broken = epubWith("<html><body><p>broken");
  auto divOnly = epubWith("<html><body><div>not a paragraph or list item</div></body></html>");
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(empty, 0, 0).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(broken, 0, 100).empty());
  EXPECT_TRUE(ChapterXPathResolver::findXPathForVisibleTextOffset(divOnly, 0, 0).empty());
}

TEST(KOReaderXPathResolver, KeepsParagraphOnlyResolutionUnchanged) {
  auto epub = epubWith(kNestedFixture);

  EXPECT_EQ(ChapterXPathResolver::findXPathForParagraph(epub, 0, 2),
            "/body/DocFragment[1]/body/div[1]/section[1]/p[2]");
}
