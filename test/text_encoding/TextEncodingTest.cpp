#include <gtest/gtest.h>

#include <cstring>

#include "Utf8.h"

namespace {
TextEncodingGuess guess(const char* s) {
  return utf8GuessEncoding(reinterpret_cast<const unsigned char*>(s), std::strlen(s));
}
}  // namespace

TEST(TextEncoding, PlainAsciiIsUndecided) { EXPECT_EQ(guess("Hello, world.\r\n"), TextEncodingGuess::Undecided); }

TEST(TextEncoding, ValidUtf8IsUtf8) {
  EXPECT_EQ(guess("Caf\xC3\xA9 \xE2\x80\x9Cquoted\xE2\x80\x9D \xF0\x9F\x93\x96"), TextEncodingGuess::Utf8);
}

TEST(TextEncoding, Cp1252AccentsAndSmartQuotesAreLegacy) {
  EXPECT_EQ(guess("Caf\xE9 au lait"), TextEncodingGuess::Legacy8Bit);  // é then a space
  EXPECT_EQ(guess("\x93quoted\x94"), TextEncodingGuess::Legacy8Bit);   // stray continuation bytes
  EXPECT_EQ(guess("na\xEFve"), TextEncodingGuess::Legacy8Bit);         // ï then ASCII
}

TEST(TextEncoding, OverlongAndSurrogateFormsAreLegacy) {
  EXPECT_EQ(guess("\xC0\xAF"), TextEncodingGuess::Legacy8Bit);
  EXPECT_EQ(guess("\xED\xA0\x80"), TextEncodingGuess::Legacy8Bit);
}

TEST(TextEncoding, ASequenceCutOffAtTheEndIsNotHeldAgainstIt) {
  EXPECT_EQ(guess("ok \xC3\xA9 then \xE2\x80"), TextEncodingGuess::Utf8);
  EXPECT_EQ(guess("ascii then \xC3"), TextEncodingGuess::Undecided);
}

TEST(TextEncoding, Cp1252ConvertsToUtf8) {
  const char in[] = "Caf\xE9 \x93hi\x94 \x80 \x81";
  EXPECT_EQ(cp1252ToUtf8(in, sizeof(in) - 1), "Caf\xC3\xA9 \xE2\x80\x9Chi\xE2\x80\x9D \xE2\x82\xAC ?");
}

TEST(TextEncoding, ConvertedTextReadsAsUtf8) {
  std::string all;
  for (int b = 0x20; b <= 0xFF; ++b) all.push_back(static_cast<char>(b));
  const std::string out = cp1252ToUtf8(all.data(), all.size());
  EXPECT_NE(utf8GuessEncoding(reinterpret_cast<const unsigned char*>(out.data()), out.size()),
            TextEncodingGuess::Legacy8Bit);
}
