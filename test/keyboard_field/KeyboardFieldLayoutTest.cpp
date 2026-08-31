#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "components/KeyboardFieldLayout.h"

namespace {

keyboard_field::Metrics metrics(const int toggleReserve = 0, const bool sideHints = false) {
  keyboard_field::Metrics m;
  m.pageWidth = 480;
  m.sideHintsWidth = 30;
  m.reserveSideHints = sideHints;
  m.widthPercent = 80;
  m.toggleReserve = toggleReserve;
  return m;
}

}  // namespace

TEST(KeyboardFieldMargin, TheFieldIsCentredOnThePage) {
  // 80% of 480 is 384, so 48 is left on either side.
  EXPECT_EQ(keyboard_field::marginFor(metrics()), 48);
}

TEST(KeyboardFieldMargin, SideButtonHintsTakeTheirWidthOffTheUsableSpan) {
  // 480 - 60 = 420 usable, 80% of which is 336, centred on the full page.
  EXPECT_EQ(keyboard_field::marginFor(metrics(0, /*sideHints=*/true)), 72);
}

TEST(KeyboardFieldMargin, AFullWidthFieldHasNoMargin) {
  keyboard_field::Metrics m = metrics();
  m.widthPercent = 100;
  EXPECT_EQ(keyboard_field::marginFor(m), 0);
}

TEST(KeyboardFieldText, TheToggleHoldsBackItsOwnWidth) {
  EXPECT_EQ(keyboard_field::textWidthFor(metrics()), 480 - 96);
  EXPECT_EQ(keyboard_field::textWidthFor(metrics(40)), 480 - 96 - 40);
}

TEST(KeyboardFieldText, TheTextAreaIsNeverNarrowerThanAPixel) {
  EXPECT_GE(keyboard_field::textWidthFor(metrics(10000)), 1);
}

TEST(KeyboardFieldLine, LeftAlignedTextStartsAtTheMargin) {
  EXPECT_EQ(keyboard_field::lineStartX(metrics(), 100, /*centred=*/false), 48);
}

TEST(KeyboardFieldLine, CentredTextSplitsTheSlack) {
  const keyboard_field::Metrics m = metrics();
  const int slack = keyboard_field::textWidthFor(m) - 100;
  EXPECT_EQ(keyboard_field::lineStartX(m, 100, /*centred=*/true), 48 + slack / 2);
}

TEST(KeyboardFieldLine, ALineWiderThanTheAreaStillStartsAtTheMargin) {
  EXPECT_EQ(keyboard_field::lineStartX(metrics(), 1000, /*centred=*/true), 48);
}

TEST(KeyboardFieldToggle, TheToggleEndsAtTheFieldsRightEdge) {
  const keyboard_field::Metrics m = metrics(44);
  EXPECT_EQ(keyboard_field::toggleX(m, 40) + 40, m.pageWidth - keyboard_field::marginFor(m));
}

TEST(KeyboardFieldToggle, TheToggleStandsClearOfTheTextArea) {
  const keyboard_field::Metrics m = metrics(44);
  const int textRight = keyboard_field::marginFor(m) + keyboard_field::textWidthFor(m);
  EXPECT_GE(keyboard_field::toggleX(m, 40), textRight);
}

TEST(KeyboardFieldTop, TheFieldSitsUnderTheHeaderWithItsOwnAir) {
  EXPECT_EQ(keyboard_field::fieldTop(/*topPadding=*/4, /*headerHeight=*/30, /*verticalSpacing=*/6,
                                     /*keyboardVerticalOffset=*/2),
            4 + 30 + 30 + 2);
}

TEST(KeyboardFieldCursor, TheBlockHugsTheCharacterOnBothSides) {
  const keyboard_field::Rect block = keyboard_field::blockCursor(/*cursorX=*/100, /*cursorY=*/50, /*charWidth=*/12,
                                                                 /*lineHeight=*/20);
  EXPECT_EQ(block.x, 99);
  EXPECT_EQ(block.width, 14);
  EXPECT_EQ(block.y, 50);
  EXPECT_EQ(block.height, 20);
}

// Source audit: the text field, its cursor and its password toggle draw through
// the same FreeInkUI target the keys do. A direct renderer call there paints in
// a font named at the call site, which a look change cannot reach, and the
// field is the one part of this screen the SDK keyboard does not own.
TEST(KeyboardFieldAudit, TheFieldDrawsThroughTheSharedTarget) {
  std::ifstream file(KEYBOARD_SOURCE);
  ASSERT_TRUE(file.is_open()) << "cannot open " << KEYBOARD_SOURCE;
  std::string line;
  int number = 0;
  while (std::getline(file, line)) {
    ++number;
    const std::size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line.compare(first, 2, "//") == 0) continue;
    for (const char* call : {"renderer.drawText", "renderer.drawCenteredText", "renderer.fillRect",
                             "renderer.drawRect", "renderer.drawLine"}) {
      EXPECT_EQ(line.find(call), std::string::npos)
          << KEYBOARD_SOURCE << ":" << number << " calls " << call
          << ". The field draws through the FreeInkUI target, placed by keyboard_field.";
    }
  }
}
