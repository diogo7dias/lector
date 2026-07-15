// Host bench for the EPUB paragraph layout engine (ParsedText -> TextBlock).
// Runs real ParsedText/TextBlock code against the deterministic GfxRenderer
// stub (10 px per codepoint, 5 px space, zero kerning), so line breaks and
// per-word x positions are exact and repeatable. The transcript golden below
// is the behavior lock for the layout-on-arena refactor: identical input must
// keep producing byte-identical transcripts before and after.

#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "Epub/ParsedText.h"
#include "Epub/blocks/TextBlock.h"

namespace {

struct LayoutResult {
  std::vector<std::shared_ptr<TextBlock>> lines;
  std::string transcript;  // "L0: word@x word@x | L1: ..." with style/focus annotations
};

LayoutResult layoutParagraph(const std::vector<std::pair<std::string, EpdFontFamily::Style>>& tokens,
                             const uint16_t viewportWidth, const bool hyphenation = false, const bool focus = false,
                             const bool guideDots = false) {
  GfxRenderer renderer;
  ParsedText text(/*extraParagraphSpacing=*/false, hyphenation, focus, guideDots);
  for (const auto& [word, style] : tokens) {
    text.addWord(word, style);
  }
  LayoutResult out;
  std::ostringstream ss;
  int lineNo = 0;
  text.layoutAndExtractLines(renderer, /*fontId=*/1, viewportWidth, [&](std::shared_ptr<TextBlock> block) {
    ASSERT_TRUE(block && block->valid());
    ss << "L" << lineNo++ << ":";
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      ss << " " << block->wordText(i) << "@" << block->wordXpos(i);
      if (block->wordStyle(i) != EpdFontFamily::REGULAR) ss << "/s" << static_cast<int>(block->wordStyle(i));
      if (block->focusBoundary(i) != 0) ss << "/f" << static_cast<int>(block->focusBoundary(i));
      if (block->guideDotXOffset(i) != 0) ss << "/g" << block->guideDotXOffset(i);
    }
    ss << "\n";
    out.lines.push_back(std::move(block));
  });
  out.transcript = ss.str();
  return out;
}

std::vector<std::pair<std::string, EpdFontFamily::Style>> plainTokens(const std::vector<std::string>& words) {
  std::vector<std::pair<std::string, EpdFontFamily::Style>> tokens;
  tokens.reserve(words.size());
  for (const auto& w : words) tokens.emplace_back(w, EpdFontFamily::REGULAR);
  return tokens;
}

// All words that went in must come out, in order, exactly once.
void expectWordsPreserved(const LayoutResult& result, const std::vector<std::string>& words) {
  std::vector<std::string> got;
  for (const auto& block : result.lines) {
    for (uint16_t i = 0; i < block->wordCount(); ++i) got.emplace_back(block->wordText(i));
  }
  EXPECT_EQ(got, words);
}

}  // namespace

TEST(ParsedTextLayout, SingleShortLine) {
  const std::vector<std::string> words = {"one", "two", "three"};
  auto result = layoutParagraph(plainTokens(words), /*viewportWidth=*/400);
  ASSERT_EQ(result.lines.size(), 1u);
  expectWordsPreserved(result, words);
  // Book-mode first-line indent = 3 spaces = 15 px with the stub metrics.
  EXPECT_EQ(result.lines[0]->wordXpos(0), 15);
  EXPECT_GT(result.lines[0]->wordXpos(1), result.lines[0]->wordXpos(0));
  EXPECT_GT(result.lines[0]->wordXpos(2), result.lines[0]->wordXpos(1));
}

TEST(ParsedTextLayout, WrapsAtViewportAndPreservesAllWords) {
  std::vector<std::string> words;
  for (int i = 0; i < 40; ++i) words.push_back("word" + std::to_string(i));
  auto result = layoutParagraph(plainTokens(words), /*viewportWidth=*/200);
  EXPECT_GT(result.lines.size(), 5u);
  expectWordsPreserved(result, words);
  // No word may start beyond the viewport.
  for (const auto& block : result.lines) {
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      EXPECT_GE(block->wordXpos(i), 0);
      EXPECT_LT(block->wordXpos(i), 200);
    }
  }
}

TEST(ParsedTextLayout, XposMonotonicWithinLine) {
  std::vector<std::string> words;
  for (int i = 0; i < 25; ++i) words.push_back("abcde");
  auto result = layoutParagraph(plainTokens(words), /*viewportWidth=*/300);
  for (const auto& block : result.lines) {
    for (uint16_t i = 1; i < block->wordCount(); ++i) {
      EXPECT_GT(block->wordXpos(i), block->wordXpos(i - 1));
    }
  }
}

TEST(ParsedTextLayout, StylesSurviveIntoBlocks) {
  std::vector<std::pair<std::string, EpdFontFamily::Style>> tokens = {
      {"plain", EpdFontFamily::REGULAR},
      {"loud", EpdFontFamily::BOLD},
      {"slanted", EpdFontFamily::ITALIC},
  };
  auto result = layoutParagraph(tokens, /*viewportWidth=*/400);
  ASSERT_EQ(result.lines.size(), 1u);
  EXPECT_EQ(result.lines[0]->wordStyle(0), EpdFontFamily::REGULAR);
  EXPECT_EQ(result.lines[0]->wordStyle(1), EpdFontFamily::BOLD);
  EXPECT_EQ(result.lines[0]->wordStyle(2), EpdFontFamily::ITALIC);
}

TEST(ParsedTextLayout, NfcCompositionAppliesToWords) {
  // "e" + COMBINING ACUTE (U+0301) must come out as precomposed U+00E9.
  auto result = layoutParagraph(plainTokens({"cafe\xcc\x81"}), /*viewportWidth=*/400);
  ASSERT_EQ(result.lines.size(), 1u);
  ASSERT_EQ(result.lines[0]->wordCount(), 1u);
  EXPECT_STREQ(result.lines[0]->wordText(0), "caf\xc3\xa9");
}

TEST(ParsedTextLayout, CjkSplitsPerCharacter) {
  // Three CJK ideographs in one token are split into breakable units (10 px
  // each with the stub metrics), and a 12 px viewport forces one per line.
  auto result = layoutParagraph(plainTokens({"\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e"}), /*viewportWidth=*/12);
  size_t totalWords = 0;
  for (const auto& block : result.lines) totalWords += block->wordCount();
  EXPECT_EQ(totalWords, 3u);
  EXPECT_EQ(result.lines.size(), 3u);
}

TEST(ParsedTextLayout, FocusReadingSetsBoundaries) {
  auto result = layoutParagraph(plainTokens({"reading", "focus", "boundaries", "everywhere"}),
                                /*viewportWidth=*/400, /*hyphenation=*/false, /*focus=*/true);
  bool anyBoundary = false;
  for (const auto& block : result.lines) {
    for (uint16_t i = 0; i < block->wordCount(); ++i) {
      if (block->focusBoundary(i) != 0) anyBoundary = true;
    }
  }
  EXPECT_TRUE(anyBoundary);
}

TEST(ParsedTextLayout, RtlWordDoesNotCrashAndIsPreserved) {
  // Hebrew "shalom" between latin words exercises the bidi reorder path.
  const std::vector<std::string> words = {"hello", "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d", "world"};
  auto result = layoutParagraph(plainTokens(words), /*viewportWidth=*/400);
  size_t totalWords = 0;
  for (const auto& block : result.lines) totalWords += block->wordCount();
  EXPECT_EQ(totalWords, 3u);
}

TEST(ParsedTextLayout, GoldenTranscriptStable) {
  // Behavior lock for the layout-on-arena refactor: this exact input with the
  // stub metrics must keep producing this exact transcript. If a DELIBERATE
  // layout change lands, regenerate the golden and say so in the commit.
  const std::vector<std::string> words = {"the", "quick", "brown", "fox",     "jumps", "over",  "the",    "lazy",
                                          "dog", "while", "seven", "wizards", "brew",  "black", "quartz", "syrup"};
  auto result = layoutParagraph(plainTokens(words), /*viewportWidth=*/160);
  expectWordsPreserved(result, words);
  const char* kGolden =
      "L0: the@15 quick@110\n"
      "L1: brown@0 fox@65 jumps@110\n"
      "L2: over@0 the@65 lazy@120\n"
      "L3: dog@0 while@45 seven@110\n"
      "L4: wizards@0 brew@120\n"
      "L5: black@0 quartz@100\n"
      "L6: syrup@0\n";
  EXPECT_EQ(result.transcript, kGolden);
}
