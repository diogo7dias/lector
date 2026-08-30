#include <gtest/gtest.h>

#include "components/ListChromeLayout.h"

namespace {

list_chrome::Metrics metrics() {
  list_chrome::Metrics m;
  m.screenWidth = 300;
  m.screenHeight = 400;
  m.topPadding = 6;
  m.headerHeight = 45;
  m.subHeaderHeight = 50;
  m.lineHeight = 20;
  m.spacing = 10;
  m.hintsHeight = 40;
  return m;
}

list_chrome::Content titleOnly() {
  list_chrome::Content c;
  c.hasHeader = true;
  return c;
}

}  // namespace

TEST(ListChromeLayout, ATitleAloneLeavesTheRestToTheBody) {
  const auto bands = list_chrome::bandsFor(metrics(), titleOnly());
  EXPECT_EQ(bands.header.y, 6);
  EXPECT_EQ(bands.header.height, 45);
  EXPECT_EQ(bands.header.width, 300);
  EXPECT_EQ(bands.contentTop, 6 + 45 + 10);
  EXPECT_EQ(bands.contentBottom, 400 - 40 - 10);
}

TEST(ListChromeLayout, NoChromeAtAllStartsAtTheTop) {
  list_chrome::Content none;
  const auto bands = list_chrome::bandsFor(metrics(), none);
  EXPECT_EQ(bands.header.height, 0);
  EXPECT_EQ(bands.contentTop, 6 + 10);
}

TEST(ListChromeLayout, ASubHeaderPushesTheBodyDownByItsOwnBand) {
  auto content = titleOnly();
  content.hasSubHeader = true;
  const auto bands = list_chrome::bandsFor(metrics(), content);
  EXPECT_EQ(bands.subHeader.y, 6 + 45);
  EXPECT_EQ(bands.subHeader.height, 50);
  EXPECT_EQ(bands.contentTop, 6 + 45 + 50 + 10);
}

TEST(ListChromeLayout, AHeaderBlockCostsOneLineEach) {
  auto content = titleOnly();
  content.headerLines = 4;
  const auto bands = list_chrome::bandsFor(metrics(), content);
  EXPECT_EQ(bands.headerLines.y, 6 + 45);
  EXPECT_EQ(bands.headerLines.height, 4 * 20);
  EXPECT_EQ(bands.contentTop, 6 + 45 + 80 + 10);
}

TEST(ListChromeLayout, ANoteSitsUnderEverythingAboveIt) {
  auto content = titleOnly();
  content.hasSubHeader = true;
  content.headerLines = 2;
  content.noteLines = 1;
  const auto bands = list_chrome::bandsFor(metrics(), content);
  EXPECT_EQ(bands.note.y, 6 + 45 + 50 + 40);
  EXPECT_EQ(bands.contentTop, bands.note.y + 20 + 10);
}

TEST(ListChromeLayout, AFootnoteEatsIntoTheBodyNotIntoTheHints) {
  auto content = titleOnly();
  content.hasFootnote = true;
  const auto bands = list_chrome::bandsFor(metrics(), content);
  EXPECT_EQ(bands.footnote.y, 400 - 40 - 20);
  EXPECT_EQ(bands.footnote.height, 20);
  EXPECT_EQ(bands.contentBottom, bands.footnote.y - 10);
}

TEST(ListChromeLayout, TheInsetsAreTheSameNumbersSeenFromTheOtherEnd) {
  auto content = titleOnly();
  content.hasSubHeader = true;
  content.hasFootnote = true;
  const auto m = metrics();
  const auto bands = list_chrome::bandsFor(m, content);
  EXPECT_EQ(list_chrome::topInsetFor(m, content), bands.contentTop);
  EXPECT_EQ(list_chrome::bottomInsetFor(m, content), m.screenHeight - bands.contentBottom);
}

TEST(ListChromeLayout, EveryBandStaysAboveTheBody) {
  auto content = titleOnly();
  content.hasSubHeader = true;
  content.headerLines = 3;
  content.noteLines = 1;
  const auto bands = list_chrome::bandsFor(metrics(), content);
  EXPECT_LE(bands.header.y + bands.header.height, bands.subHeader.y);
  EXPECT_LE(bands.subHeader.y + bands.subHeader.height, bands.headerLines.y);
  EXPECT_LE(bands.headerLines.y + bands.headerLines.height, bands.note.y);
  EXPECT_LE(bands.note.y + bands.note.height, bands.contentTop);
}

TEST(ListChromeLayout, MoreChromeThanPanelLeavesAnEmptyBodyNotAnInsideOutOne) {
  auto m = metrics();
  m.screenHeight = 100;
  auto content = titleOnly();
  content.hasSubHeader = true;
  content.headerLines = 4;
  content.hasFootnote = true;
  const auto bands = list_chrome::bandsFor(m, content);
  EXPECT_GE(bands.contentBottom, bands.contentTop);
}
