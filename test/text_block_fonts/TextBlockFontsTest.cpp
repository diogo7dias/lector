// Screens that draw over the page (dictionary select, quote select, quote underline) take
// their geometry from these calls, so they must land where render() drew the word.
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include "blocks/TextBlock.h"

namespace {
constexpr int SECTION = 10;
constexpr int BIG = 20;
constexpr int SMALL = 5;

std::unique_ptr<TextBlock> line(const std::vector<EpdFontFamily::Style>& styles = {EpdFontFamily::REGULAR,
                                                                                   EpdFontFamily::REGULAR,
                                                                                   EpdFontFamily::REGULAR},
                                std::vector<std::string> ruby = {}) {
  return std::make_unique<TextBlock>(std::vector<std::string>{"aa", "bbb", "c"}, std::vector<int16_t>{0, 50, 200},
                                     styles, std::vector<uint8_t>{}, std::vector<uint16_t>{}, std::vector<uint16_t>{},
                                     BlockStyle(), std::move(ruby));
}

std::unique_ptr<TextBlock> mixedLine() {
  auto block = line();
  const uint8_t slots[] = {0, 1, 2};  // section, BIG, SMALL
  const int32_t fonts[TextBlock::MAX_WORD_FONTS] = {BIG, SMALL, 0};
  EXPECT_TRUE(block->setWordFonts(slots, fonts));
  return block;
}
}  // namespace

TEST(TextBlockFonts, PlainLineSitsAtTheLineTopInTheSectionFont) {
  const GfxRenderer renderer;
  const auto block = line();
  EXPECT_EQ(0, block->wordBoxTop(renderer, SECTION, 1));
  EXPECT_EQ(SECTION, block->wordRenderFont(renderer, SECTION, 1));
  EXPECT_EQ(30, block->wordWidth(renderer, SECTION, 1));
}

TEST(TextBlockFonts, ResizedWordsShareTheTallestBaseline) {
  const GfxRenderer renderer;
  const auto block = mixedLine();
  EXPECT_EQ(2 * BIG, block->lineAscent(renderer, SECTION));
  for (uint16_t i = 0; i < 3; i++) {
    const int font = block->wordRenderFont(renderer, SECTION, i);
    EXPECT_EQ(2 * BIG, block->wordBoxTop(renderer, SECTION, i) + renderer.getFontAscenderSize(font)) << i;
  }
  EXPECT_EQ(3 * BIG, block->wordWidth(renderer, SECTION, 1));
  EXPECT_EQ(SMALL, block->wordWidth(renderer, SECTION, 2));
}

TEST(TextBlockFonts, WordFontThatCannotLoadFallsBackToTheSectionFont) {
  GfxRenderer renderer;
  renderer.missingFont = BIG;
  const auto block = mixedLine();
  EXPECT_EQ(SECTION, block->wordRenderFont(renderer, SECTION, 1));
  EXPECT_EQ(0, block->wordBoxTop(renderer, SECTION, 1));
}

TEST(TextBlockFonts, RubyGapMovesTheBoxDown) {
  const GfxRenderer renderer;
  const auto block = line({EpdFontFamily::REGULAR, EpdFontFamily::REGULAR, EpdFontFamily::REGULAR}, {"r", "", ""});
  EXPECT_EQ(2 * SECTION / 2, block->wordBoxTop(renderer, SECTION, 1));
}

TEST(TextBlockFonts, WordExpansionWidensTheWord) {
  const GfxRenderer renderer;
  const auto block = line();
  block->setLetterSpacing(2);
  EXPECT_EQ(30 + 2 * 2, block->wordWidth(renderer, SECTION, 1));
  EXPECT_EQ(SECTION, block->wordWidth(renderer, SECTION, 2));  // one glyph has no gap
}

// Superscripts keep the line's box but draw raised, as render() raises them.
TEST(TextBlockFonts, DrawWordMatchesRenderForEveryWord) {
  const auto styles = std::vector<EpdFontFamily::Style>{EpdFontFamily::REGULAR, EpdFontFamily::SUP, EpdFontFamily::SUB};
  auto block = line(styles);
  const uint8_t slots[] = {0, 1, 2};
  const int32_t fonts[TextBlock::MAX_WORD_FONTS] = {BIG, SMALL, 0};
  ASSERT_TRUE(block->setWordFonts(slots, fonts));
  block->setLetterSpacing(1);

  constexpr int X = 7;
  constexpr int Y = 100;
  const GfxRenderer page;
  block->render(page, SECTION, X, Y);
  ASSERT_EQ(3u, page.drawn.size());

  const GfxRenderer over;
  for (uint16_t i = 0; i < 3; i++) {
    block->drawWord(over, SECTION, i, X + block->wordXpos(i), Y + block->wordBoxTop(over, SECTION, i), false);
    ASSERT_EQ(i + 1u, over.drawn.size());
    EXPECT_EQ(page.drawn[i].font, over.drawn[i].font) << i;
    EXPECT_EQ(page.drawn[i].x, over.drawn[i].x) << i;
    EXPECT_EQ(page.drawn[i].y, over.drawn[i].y) << i;
    EXPECT_EQ(page.drawn[i].letterSpacing, over.drawn[i].letterSpacing) << i;
    EXPECT_TRUE(page.drawn[i].black);
    EXPECT_FALSE(over.drawn[i].black);
  }
}
