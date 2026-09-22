#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <fstream>
#include <iterator>
#include <sstream>

#include "ParsedText.h"

namespace {
struct Line {
  std::vector<std::string> words;
  std::vector<int16_t> x;
  std::vector<EpdFontFamily::Style> styles;
  std::vector<uint8_t> focus;
  std::vector<uint16_t> suffix;
  std::vector<uint16_t> dots;
};
std::vector<Line> lines;
const GfxRenderer renderer;
constexpr int FONT = 0;
constexpr const char* PROSE =
    "alpha beta gamma delta epsilon zeta eta theta iota kappa lambda mu nu xi omicron pi rho sigma tau";

void layout(const std::string& text, int percent, int width, CssTextAlign align = CssTextAlign::Left,
            uint8_t dots = GUIDE_DOTS_OFF, bool rtl = false, bool styled = false) {
  lines.clear();
  BlockStyle style;
  style.alignment = align;
  style.textAlignDefined = true;
  style.directionDefined = true;
  style.isRtl = rtl;
  ParsedText parsed(false, false, dots, style, 1, 0, percent);
  std::istringstream input(text);
  std::string word;
  int i = 0;
  while (input >> word) {
    const auto fontStyle = styled && ++i % 2 == 0 ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    // ChapterHtmlSlimParser::characterData emits NBSP as an attached space token.
    size_t start = 0;
    size_t nbsp;
    while ((nbsp = word.find("\xC2\xA0", start)) != std::string::npos) {
      parsed.addWord(word.substr(start, nbsp - start), fontStyle, false, start != 0);
      parsed.addWord(" ", fontStyle, false, true);
      start = nbsp + 2;
    }
    parsed.addWord(word.substr(start), fontStyle, false, start != 0);
  }
  parsed.layoutAndExtractLines(renderer, FONT, width, [](void*, std::shared_ptr<TextBlock>, uint32_t) {}, nullptr);
}

int rightEdge(const Line& line) {
  int edge = 0;
  for (size_t i = 0; i < line.words.size(); ++i)
    edge = std::max(edge, line.x[i] + renderer.getTextAdvanceX(FONT, line.words[i].c_str(), line.styles[i]));
  return edge;
}

// Unambiguous byte snapshot of every word, position, style and optional annotation.
std::string snapshot() {
  std::ostringstream out;
  for (const auto& line : lines) {
    for (size_t i = 0; i < line.words.size(); ++i) {
      out << line.words[i].size() << ':' << line.words[i] << ',' << line.x[i] << ',' << int(line.styles[i]) << ','
          << (line.focus.empty() ? 0 : int(line.focus[i])) << ',' << (line.suffix.empty() ? 0 : line.suffix[i]) << ','
          << (line.dots.empty() ? 0 : line.dots[i]) << ';';
    }
    out << '\n';
  }
  return out.str();
}

class WordSpacing : public testing::TestWithParam<int> {};
INSTANTIATE_TEST_SUITE_P(EverySetting, WordSpacing, testing::Range(75, 151, 5));
}  // namespace

// Capture the production layout's constructor arguments at its output boundary.
// Storage/rendering are hardware services, not a second implementation of layout.
TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& xpos,
                     const std::vector<EpdFontFamily::Style>& styles, const std::vector<uint8_t>& focus,
                     const std::vector<uint16_t>& suffix, const std::vector<uint16_t>& dots, const BlockStyle&,
                     std::vector<std::string>) {
  lines.push_back({words, xpos, styles, focus, suffix, dots});
}

TEST_P(WordSpacing, MeasurementAndPositioningAgree) {
  const int percent = GetParam();
  const int gap = (7 * percent + 50) / 100 - 2;  // a -> space -> b kerning remains -2
  const int measured = 10 + gap + 10;
  // First line keeps the existing three-space indent (21px).
  layout("a b", percent, measured + 21, CssTextAlign::Left);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ(measured + 21, rightEdge(lines[0]));
  EXPECT_EQ(gap, lines[0].x[1] - lines[0].x[0] - 10);
  layout("a b", percent, measured + 20, CssTextAlign::Left);
  EXPECT_EQ(2u, lines.size());  // one pixel less cannot hold the measured line
  layout("a b", percent, 200, CssTextAlign::Right);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ(200, rightEdge(lines[0]));
  EXPECT_EQ(200 - measured, lines[0].x[0]);
}

TEST(WordSpacing, WiderSettingMeasuresWider) {
  layout("a b", 75, 200, CssTextAlign::Right);
  const int narrowWidth = 200 - lines[0].x[0];
  layout("a b", 150, 200, CssTextAlign::Right);
  EXPECT_GT(200 - lines[0].x[0], narrowWidth);
}

TEST_P(WordSpacing, JustificationFlushAndLastLineNatural) {
  for (uint8_t dots : {GUIDE_DOTS_OFF, GUIDE_DOTS_VISIBLE, GUIDE_DOTS_HIDDEN}) {
    layout(PROSE, GetParam(), 260, CssTextAlign::Justify, dots, false, true);
    ASSERT_GT(lines.size(), 2u);
    for (size_t i = 0; i + 1 < lines.size(); ++i) {
      ASSERT_GT(lines[i].words.size(), 1u);
      EXPECT_EQ(260, rightEdge(lines[i])) << "line=" << i << " dots=" << int(dots);
    }
    EXPECT_LT(rightEdge(lines.back()), 260);
  }
  layout("a b", GetParam(), 200, CssTextAlign::Justify);
  EXPECT_EQ(21 + 20 + (7 * GetParam() + 50) / 100 - 2, rightEdge(lines[0]));
}

TEST_P(WordSpacing, GuideDotsScaleSpacesAndHiddenMatchesVisible) {
  const int gap = 2 * ((7 * GetParam() + 50) / 100) - 2 + 3;
  layout("a b", GetParam(), 41 + gap, CssTextAlign::Left, GUIDE_DOTS_VISIBLE);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ(41 + gap, rightEdge(lines[0]));
  EXPECT_EQ(10 + (gap - 3) / 2, lines[0].dots[0]);
  const auto positions = lines[0].x;
  layout("a b", GetParam(), 41 + gap, CssTextAlign::Left, GUIDE_DOTS_HIDDEN);
  ASSERT_EQ(1u, lines.size());
  EXPECT_EQ(positions, lines[0].x);
  EXPECT_TRUE(lines[0].dots.empty());
  layout("a b", GetParam(), 40 + gap, CssTextAlign::Left, GUIDE_DOTS_VISIBLE);
  EXPECT_EQ(2u, lines.size());
}

TEST_P(WordSpacing, NbspStaysAttachedAndScales) {
  const int measured = 20 + (7 * GetParam() + 50) / 100 - 2;
  layout(
      "a\xC2\xA0"
      "b",
      GetParam(), measured + 21, CssTextAlign::Left);
  ASSERT_EQ(1u, lines.size());
  ASSERT_EQ(3u, lines[0].words.size());
  EXPECT_EQ(" ", lines[0].words[1]);
  EXPECT_EQ(measured + 21, rightEdge(lines[0]));
  layout(
      "x a\xC2\xA0"
      "b",
      GetParam(), measured + 21, CssTextAlign::Left);
  ASSERT_EQ(2u, lines.size());
  EXPECT_EQ("a", lines[1].words.front());
  EXPECT_EQ("b", lines[1].words.back());
  layout(
      "a\xC2\xA0"
      "b c d e f g h i j k",
      GetParam(), 103, CssTextAlign::Justify);
  ASSERT_GT(lines.size(), 1u);
  for (size_t i = 0; i + 1 < lines.size(); ++i) EXPECT_EQ(103, rightEdge(lines[i]));
}

TEST_P(WordSpacing, CjkKeepsZeroWidthBoundariesAndFlushJustification) {
  for (uint8_t dots : {GUIDE_DOTS_OFF, GUIDE_DOTS_VISIBLE, GUIDE_DOTS_HIDDEN}) {
    layout("天地玄黃宇宙洪荒日月盈昃辰宿列張", 100, 103, CssTextAlign::Justify, dots);
    const auto unchanged = snapshot();
    layout("天地玄黃宇宙洪荒日月盈昃辰宿列張", GetParam(), 103, CssTextAlign::Justify, dots);
    EXPECT_EQ(unchanged, snapshot());
    ASSERT_GT(lines.size(), 1u);
    for (size_t i = 0; i + 1 < lines.size(); ++i) EXPECT_EQ(103, rightEdge(lines[i]));
    EXPECT_TRUE(lines.back().dots.empty());
  }
}

TEST_P(WordSpacing, RtlAndBidiKeepFlushMargins) {
  for (bool rtl : {false, true}) {
    for (const char* text : {PROSE, "אחד two שלוש four חמש six שבע eight תשע ten אחד two שלוש four"}) {
      layout(text, GetParam(), 200, CssTextAlign::Justify, GUIDE_DOTS_VISIBLE, rtl);
      ASSERT_GT(lines.size(), 1u);
      for (size_t i = 0; i + 1 < lines.size(); ++i) {
        // The existing first-line indent reduces RTL's effective right edge.
        EXPECT_EQ(rtl && i == 0 ? 179 : 200, rightEdge(lines[i]));
        if (text != PROSE || rtl) {
          EXPECT_TRUE(lines[i].dots.empty());
        }
      }
    }
  }
}

TEST(WordSpacing, HundredPercentIsByteIdenticalToPreChangeLayout) {
  std::string bytes;
  for (const char* text : {PROSE,
                           "a\xC2\xA0"
                           "b c d e f g h i j k",
                           "天地玄黃宇宙洪荒日月盈昃辰宿列張", "אחד two שלוש four חמש six שבע eight תשע ten"}) {
    for (auto align : {CssTextAlign::Left, CssTextAlign::Right, CssTextAlign::Center, CssTextAlign::Justify}) {
      for (uint8_t dots : {GUIDE_DOTS_OFF, GUIDE_DOTS_VISIBLE, GUIDE_DOTS_HIDDEN}) {
        layout(text, 100, 203, align, dots, false, true);
        bytes += snapshot() + "---\n";
      }
    }
  }
  // Captured with this harness and ParsedText.cpp/.h from c9b81d375f7e5cd0b604b60e54121d79176b1d08.
  // Retain only the non-hyphenating cases from that snapshot.
  std::ifstream baseline(WORD_SPACING_BASELINE);
  ASSERT_TRUE(baseline.good());
  const std::string expected{std::istreambuf_iterator<char>(baseline), std::istreambuf_iterator<char>()};
  EXPECT_EQ(expected, bytes);
}

TEST(WordSpacing, InvalidPercentagesClampBeforeLayout) {
  layout("a b", 75, 200);
  const auto minimum = snapshot();
  layout("a b", 0, 200);
  EXPECT_EQ(minimum, snapshot());
  layout("a b", 150, 200);
  const auto maximum = snapshot();
  layout("a b", 255, 200);
  EXPECT_EQ(maximum, snapshot());
}

TEST(WordSpacing, WordsStayWholeAndSoftHyphensStayInvisible) {
  for (const char* text : {"extraordinary", "extra\xC2\xADordinary", "extra-ordinary"}) {
    // A word wider than the column must not invoke the old fallback splitter.
    layout(text, 100, 70);
    ASSERT_EQ(1u, lines.size());
    ASSERT_EQ(1u, lines[0].words.size());
    EXPECT_EQ(std::string(text).find('-') == std::string::npos ? "extraordinary" : "extra-ordinary", lines[0].words[0]);
  }
}

TEST(WordSpacing, SoftFlushedParagraphIndentsOnlyItsFirstLine) {
  // ChapterHtmlSlimParser flushes a long paragraph in pieces (includeLastLine=false),
  // then appends more words and lays the rest out with a final full pass.
  lines.clear();
  BlockStyle style;
  style.alignment = CssTextAlign::Left;
  style.textAlignDefined = true;
  style.directionDefined = true;
  ParsedText parsed(false, false, GUIDE_DOTS_OFF, style, 1, 0, 100);
  const auto sink = [](void*, std::shared_ptr<TextBlock>, uint32_t) {};
  for (int pass = 0; pass < 3; ++pass) {
    std::istringstream input(PROSE);
    std::string word;
    while (input >> word) parsed.addWord(word, EpdFontFamily::REGULAR);
    parsed.layoutAndExtractLines(renderer, FONT, 200, sink, nullptr, pass == 2);
  }
  ASSERT_GT(lines.size(), 4u);
  EXPECT_EQ(21, lines[0].x[0]);
  for (size_t i = 1; i < lines.size(); ++i) EXPECT_EQ(0, lines[i].x[0]) << "line=" << i;
}
