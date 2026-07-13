#include "ParsedText.h"

#include <Arena.h>
#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Utf8.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

#include "hyphenation/Hyphenator.h"

constexpr int MAX_COST = std::numeric_limits<int>::max();

namespace {

// Soft hyphen byte pattern used throughout EPUBs (UTF-8 for U+00AD).
constexpr char SOFT_HYPHEN_UTF8[] = "\xC2\xAD";
constexpr size_t SOFT_HYPHEN_BYTES = 2;
// Guide dot: a middle dot (U+00B7) drawn in the widened gap between words.
constexpr char GUIDE_DOT_UTF8[] = "\xC2\xB7";
constexpr uint32_t GUIDE_DOT_CODEPOINT = 0x00B7;
// Paragraph-level direction: scan the first N words to find base direction.
constexpr size_t RTL_PARAGRAPH_PROBE_WORDS = 3;
// Per-word: scan enough chars to see through leading neutrals (quotes, numbers)
// before giving up. 64 is a hedge for pathological cases like long numeric tokens.
constexpr int RTL_PER_WORD_PROBE_DEPTH = 64;
constexpr size_t MIN_JUSTIFY_GAPS = 1;

// Byte-level pre-check: Hebrew UTF-8 lead bytes 0xD6-0xD7, Arabic/Syriac 0xD8-0xDB.
bool mayContainRtlBytes(const char* str) {
  for (const auto* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
    if (*p >= 0xD6 && *p <= 0xDB) return true;
  }
  return false;
}

// Returns the first rendered codepoint of a word (skipping leading soft hyphens).
uint32_t firstCodepoint(const std::string& word) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  while (true) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) return 0;
    if (cp != 0x00AD) return cp;  // skip soft hyphens
  }
}

// Returns the last codepoint of a word by scanning backward for the start of the last UTF-8 sequence.
uint32_t lastCodepoint(const std::string& word) {
  if (word.empty()) return 0;
  // UTF-8 continuation bytes start with 10xxxxxx; scan backward to find the leading byte.
  size_t i = word.size() - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

bool containsSoftHyphen(const std::string& word) { return word.find(SOFT_HYPHEN_UTF8) != std::string::npos; }

bool isNoBreakBeforeCjkPunctuation(const uint32_t cp) {
  switch (cp) {
    case '.':
    case ',':
    case ':':
    case ';':
    case '!':
    case '?':
    case ')':
    case ']':
    case '}':
    case 0x00BB:  // »
    case 0x2019:  // ’
    case 0x201D:  // ”
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0x3009:  // 〉
    case 0x300B:  // 》
    case 0x300D:  // 」
    case 0x300F:  // 』
    case 0x3011:  // 】
    case 0x3015:  // 〕
    case 0x3017:  // 〗
    case 0x3019:  // 〙
    case 0x301B:  // 〛
    case 0xFF01:  // ！
    case 0xFF09:  // ）
    case 0xFF0C:  // ，
    case 0xFF0E:  // ．
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1F:  // ？
    case 0xFF3D:  // ］
    case 0xFF5D:  // ｝
      return true;
    default:
      return false;
  }
}

bool isNoBreakAfterCjkPunctuation(const uint32_t cp) {
  switch (cp) {
    case '(':
    case '[':
    case '{':
    case 0x00AB:  // «
    case 0x2018:  // ‘
    case 0x201C:  // “
    case 0x3008:  // 〈
    case 0x300A:  // 《
    case 0x300C:  // 「
    case 0x300E:  // 『
    case 0x3010:  // 【
    case 0x3014:  // 〔
    case 0x3016:  // 〖
    case 0x3018:  // 〘
    case 0x301A:  // 〚
    case 0xFF08:  // （
    case 0xFF3B:  // ［
    case 0xFF5B:  // ｛
      return true;
    default:
      return false;
  }
}

bool containsCjkBreakableCodepoint(const std::string& text) {
  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  while (*ptr) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (utf8IsCjkBreakable(cp)) {
      return true;
    }
  }
  return false;
}

bool hasCjkBreakOpportunityBetween(const uint32_t leftCp, const uint32_t rightCp) {
  if (!utf8IsCjkBreakable(leftCp) && !utf8IsCjkBreakable(rightCp)) return false;
  if (isNoBreakAfterCjkPunctuation(leftCp) || isNoBreakBeforeCjkPunctuation(rightCp)) return false;
  if (utf8IsCombiningMark(rightCp)) return false;
  return true;
}

std::vector<size_t> cjkCharacterBreakByteOffsets(const std::string& text) {
  struct CodepointBoundary {
    uint32_t cp;
    size_t endOffset;
  };

  std::vector<CodepointBoundary> codepoints;
  codepoints.reserve(text.size());
  bool hasCjkBreakable = false;

  const auto* ptr = reinterpret_cast<const unsigned char*>(text.c_str());
  const auto* const start = ptr;
  while (*ptr) {
    const uint32_t cp = utf8NextCodepoint(&ptr);
    if (cp == 0) break;
    if (utf8IsCjkBreakable(cp)) {
      hasCjkBreakable = true;
    }
    codepoints.push_back({cp, static_cast<size_t>(ptr - start)});
  }

  if (!hasCjkBreakable || codepoints.size() < 2) return {};

  std::vector<size_t> allowedOffsets;
  allowedOffsets.reserve(codepoints.size() - 1);
  for (size_t i = 0; i + 1 < codepoints.size(); ++i) {
    const uint32_t current = codepoints[i].cp;
    const uint32_t next = codepoints[i + 1].cp;
    if (!hasCjkBreakOpportunityBetween(current, next)) continue;
    allowedOffsets.push_back(codepoints[i].endOffset);
  }
  return allowedOffsets;
}

int computeJustifyExtra(const int spareSpace, const size_t gapCount) {
  if (gapCount < MIN_JUSTIFY_GAPS || spareSpace <= 0) return 0;
  // Distribute the spare space evenly across gaps. Do NOT bail out to 0 when the
  // per-gap stretch is large: a sparse line (few words on a wide page) legitimately
  // needs big gaps to reach the margin. Returning 0 there disables justification for
  // that line, leaving it right-aligned (RTL) / left-aligned (LTR) — the mismatched
  // alignment bug. Match the un-capped behavior of the old code.
  return spareSpace / static_cast<int>(gapCount);
}

// Removes every soft hyphen in-place so rendered glyphs match measured widths.
void stripSoftHyphensInPlace(std::string& word) {
  size_t pos = 0;
  while ((pos = word.find(SOFT_HYPHEN_UTF8, pos)) != std::string::npos) {
    word.erase(pos, SOFT_HYPHEN_BYTES);
  }
}

// Returns the advance width for a word while ignoring soft hyphen glyphs and optionally appending a visible hyphen.
// Uses advance width (sum of glyph advances + kerning) rather than bounding box width so that italic glyph overhangs
// don't inflate inter-word spacing.
uint16_t measureWordWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                          const EpdFontFamily::Style style, const bool appendHyphen = false) {
  if (word.size() == 1 && word[0] == ' ' && !appendHyphen) {
    return renderer.getSpaceWidth(fontId, style);
  }
  const bool hasSoftHyphen = containsSoftHyphen(word);
  if (!hasSoftHyphen && !appendHyphen) {
    return renderer.getTextAdvanceX(fontId, word.c_str(), style);
  }

  std::string sanitized = word;
  if (hasSoftHyphen) {
    stripSoftHyphensInPlace(sanitized);
  }
  if (appendHyphen) {
    sanitized.push_back('-');
  }
  return renderer.getTextAdvanceX(fontId, sanitized.c_str(), style);
}

// Checks if a UTF-8 codepoint should be counted as part of a word for Focus Reading
bool isWordCharacter(uint32_t cp) {
  // ASCII range (Catches 95%+ of characters immediately)
  if (cp < 128) {
    // Bitwise trick: (cp | 0x20) converts uppercase ASCII to lowercase.
    // This checks for A-Z and a-z mathematically, avoiding memory lookups and <cctype>
    return ((cp | 0x20) >= 'a' && (cp | 0x20) <= 'z') || cp == '\'';
  }

  // General Punctuation Block, Currency, Math, Arrows, & Symbols (0x2000 - 0x2BFF)
  if (cp >= 0x2000 && cp <= 0x2BFF) {
    // Explicitly allow smart quotes, reject all other general punctuation (em-dashes, etc.)
    return cp == 0x2018 || cp == 0x2019;
  }

  // Latin-1 Punctuation Block (0x00A1 - 0x00BF)
  if (cp >= 0x00A1 && cp <= 0x00BF) {
    // Allow ordinal indicators and micro sign, reject the rest (¡, ¿, «, », etc.)
    return cp == 0x00AA || cp == 0x00B5 || cp == 0x00BA;
  }

  // Rejects Two-em dash, Three-em dash, Double oblique hyphen, etc.
  if (cp >= 0x2E00 && cp <= 0x2E7F) return false;

  // Rejects Modifier Minus (0x02D7), Small Hyphen (0xFE63), and Fullwidth Hyphen (0xFF0D)
  if (cp == 0x02D7 || cp == 0xFE63 || cp == 0xFF0D) return false;
  // Assume all other Unicode ranges (accented letters, Cyrillic, Greek, etc.) are valid

  return true;
}

// Focus Reading bolds the first ~43% of a word's codepoints. Same anchors as CrossInk
// (1 bold at 4 chars, 3 bold at 7) so focus-reading bold length matches CrossInk exactly.
constexpr size_t FOCUS_READING_PERCENT = 43;

struct FocusTokenMetadata {
  EpdFontFamily::Style style;  // baseStyle, or baseStyle|BOLD when the whole word is bold
  uint8_t boundary;            // UTF-8 byte offset where the regular suffix starts; 0 = no split
};

// Computes the bold-prefix boundary for one word segment without splitting it into two
// tokens: the word stays whole and carries this byte offset. boundary 0 means "no split"
// (feature off, already-bold text, empty, or the whole word is bold).
FocusTokenMetadata computeFocusMetadata(const std::string_view segment, const EpdFontFamily::Style baseStyle,
                                        const bool focusReadingEnabled) {
  if (!focusReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0 || segment.empty()) {
    return {baseStyle, 0};
  }

  size_t charCount = 0;
  const auto* countPtr = reinterpret_cast<const unsigned char*>(segment.data());
  const auto* const countEnd = countPtr + segment.length();
  while (countPtr < countEnd) {
    const auto* const cpStart = countPtr;
    const uint32_t cp = utf8NextCodepoint(&countPtr);
    if (!isWordCharacter(cp)) break;
    if (countPtr <= cpStart) break;
    charCount++;
  }

  if (charCount == 0) {
    return {baseStyle, 0};
  }

  size_t targetBoldChars = (charCount * FOCUS_READING_PERCENT) / 100;
  targetBoldChars = std::clamp<size_t>(targetBoldChars, 1, 9);

  if (targetBoldChars >= charCount) {
    return {static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::BOLD), 0};
  }

  const auto* splitPtr = reinterpret_cast<const unsigned char*>(segment.data());
  for (size_t i = 0; i < targetBoldChars; ++i) {
    utf8NextCodepoint(&splitPtr);
  }
  const size_t splitByteOffset = splitPtr - reinterpret_cast<const unsigned char*>(segment.data());
  return {baseStyle, static_cast<uint8_t>(std::min<size_t>(splitByteOffset, UINT8_MAX))};
}

// First codepoint at or after byteOffset (the first codepoint of the regular suffix).
uint32_t firstCodepointAtByteOffset(const std::string& word, const size_t byteOffset) {
  if (byteOffset >= word.size()) return 0;
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + byteOffset);
  return utf8NextCodepoint(&ptr);
}

// Last codepoint ending before byteOffset (the last codepoint of the bold prefix).
uint32_t lastCodepointBeforeByteOffset(const std::string& word, const size_t byteOffset) {
  if (word.empty() || byteOffset == 0) return 0;
  size_t i = std::min(byteOffset, word.size()) - 1;
  while (i > 0 && (static_cast<uint8_t>(word[i]) & 0xC0) == 0x80) {
    --i;
  }
  const auto* ptr = reinterpret_cast<const unsigned char*>(word.c_str() + i);
  return utf8NextCodepoint(&ptr);
}

// X advance to the start of the regular suffix of a focus-split word: bold prefix
// advance plus the kerning between the last prefix glyph and the first suffix glyph,
// both measured in the bold style (the prefix is rendered bold). Returns 0 when the
// word is not focus-split (boundary 0) or the boundary is out of range.
int measureFocusSuffixX(const GfxRenderer& renderer, const int fontId, const std::string& word,
                        const EpdFontFamily::Style style, const uint8_t boundary) {
  if (boundary == 0 || boundary >= word.size()) {
    return 0;
  }

  const auto boldStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
  char prefixBuf[40];
  const size_t prefixLen = std::min<size_t>(boundary, sizeof(prefixBuf) - 1);
  memcpy(prefixBuf, word.c_str(), prefixLen);
  prefixBuf[prefixLen] = '\0';

  const int prefixWidth = renderer.getTextAdvanceX(fontId, prefixBuf, boldStyle);
  const int kern = renderer.getKerning(fontId, lastCodepointBeforeByteOffset(word, boundary),
                                       firstCodepointAtByteOffset(word, boundary), boldStyle);
  return prefixWidth + kern;
}

// Advance width of a single word token that may be focus-split: bold prefix + regular
// suffix measured as one unit. Falls back to plain measureWordWidth when the word is not
// focus-split, is being hyphenated, or carries a soft hyphen (all of which keep a single
// style). Equivalent to measureWordWidth when focusBoundary is 0.
uint16_t measureTokenWidth(const GfxRenderer& renderer, const int fontId, const std::string& word,
                           const EpdFontFamily::Style style, const uint8_t focusBoundary,
                           const bool appendHyphen = false) {
  if (focusBoundary == 0 || focusBoundary >= word.size() || appendHyphen || containsSoftHyphen(word)) {
    return measureWordWidth(renderer, fontId, word, style, appendHyphen);
  }

  const int suffixX = measureFocusSuffixX(renderer, fontId, word, style, focusBoundary);
  const int suffixWidth = renderer.getTextAdvanceX(fontId, word.c_str() + focusBoundary, style);
  return static_cast<uint16_t>(std::max(0, suffixX + suffixWidth));
}

// Advance width of the guide dot glyph itself (always drawn in the regular style).
int guideDotAdvance(const GfxRenderer& renderer, const int fontId) {
  return renderer.getTextAdvanceX(fontId, GUIDE_DOT_UTF8, EpdFontFamily::REGULAR);
}

// The full widened gap that holds a guide dot: space(leftWord -> dot) + dot glyph
// + space(dot -> rightWord). Replaces the plain inter-word space when a dot sits
// between two words. No word-spacing delta is applied to a guide-dot gap.
int guideDotNaturalGap(const GfxRenderer& renderer, const int fontId, const std::string& leftWord,
                       const std::string& rightWord, const EpdFontFamily::Style leftStyle) {
  return renderer.getSpaceAdvance(fontId, lastCodepoint(leftWord), GUIDE_DOT_CODEPOINT, leftStyle) +
         guideDotAdvance(renderer, fontId) +
         renderer.getSpaceAdvance(fontId, GUIDE_DOT_CODEPOINT, firstCodepoint(rightWord), EpdFontFamily::REGULAR);
}

}  // namespace

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle, const bool underline,
                         const bool attachToPrevious) {
  if (word.empty()) return;

  // The device fonts carry no combining-mark positioning, so EPUB text stored in NFD
  // (a base letter followed by separate combining accents -- common for Vietnamese,
  // and used for many EPUB <h1> chapter headings) renders with the marks detached or
  // misplaced. Compose to NFC here, the single funnel every word passes through, so a
  // precomposed glyph is used instead. This runs once per word at layout time (the
  // result is cached in the section file) and is a cheap no-op for mark-free text.
  word = utf8ComposeNfc(word);

  EpdFontFamily::Style baseStyle = fontStyle;
  if (underline) {
    baseStyle = static_cast<EpdFontFamily::Style>(baseStyle | EpdFontFamily::UNDERLINE);
  }
  const bool wordStartsRtl = !hasRtlWord && mayContainRtlBytes(word.c_str()) &&
                             BidiUtils::startsWithRtl(word.c_str(), RTL_PER_WORD_PROBE_DEPTH);

  // Guide dot: a virtual middle dot (U+00B7) belongs in the gap before the next
  // real token. Set once effectiveAttach/NoSpaceBefore are known below; every push
  // consumes and clears it so only the first token of a word carries the dot.
  bool guideDotBeforeNextToken = false;
  const auto pushToken = [&](std::string token, const bool continues, const bool noSpaceBefore,
                             const uint8_t focusBoundary = 0) {
    words.push_back(std::move(token));
    wordStyles.push_back(baseStyle);
    wordContinues.push_back(continues);
    wordNoSpaceBefore.push_back(noSpaceBefore);
    wordFocusBoundary.push_back(focusBoundary);
    wordGuideDotBefore.push_back(guideDotBeforeNextToken);
    guideDotBeforeNextToken = false;
  };

  bool effectiveAttachToPrevious = attachToPrevious;
  bool effectiveNoSpaceBefore = false;
  if (attachToPrevious && !words.empty() &&
      hasCjkBreakOpportunityBetween(lastCodepoint(words.back()), firstCodepoint(word))) {
    effectiveAttachToPrevious = false;
    effectiveNoSpaceBefore = true;
  }

  // A guide dot precedes this word only when it starts a fresh space-separated
  // token (not attached, not a no-space break) and is not the first word on the line.
  if (guideReadingEnabled && !effectiveAttachToPrevious && !effectiveNoSpaceBefore && !words.empty()) {
    guideDotBeforeNextToken = true;
  }

  if (auto breakOffsets = cjkCharacterBreakByteOffsets(word); !breakOffsets.empty()) {
    bool firstToken = true;
    size_t tokenStart = 0;
    for (const size_t breakOffset : breakOffsets) {
      if (breakOffset <= tokenStart || breakOffset > word.size()) continue;
      pushToken(word.substr(tokenStart, breakOffset - tokenStart), firstToken ? effectiveAttachToPrevious : false,
                firstToken ? effectiveNoSpaceBefore : true);
      firstToken = false;
      tokenStart = breakOffset;
    }
    if (tokenStart < word.size()) {
      pushToken(word.substr(tokenStart), firstToken ? effectiveAttachToPrevious : false,
                firstToken ? effectiveNoSpaceBefore : true);
    }
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  if (containsCjkBreakableCodepoint(word)) {
    pushToken(std::move(word), effectiveAttachToPrevious, effectiveNoSpaceBefore);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // Already-bold text should stay fully bold; focus splitting would make its suffix regular later.
  if (!this->focusReadingEnabled || (baseStyle & EpdFontFamily::BOLD) != 0) {
    pushToken(std::move(word), effectiveAttachToPrevious, effectiveNoSpaceBefore);
    if (wordStartsRtl) {
      hasRtlWord = true;
    }
    return;
  }

  // --- FOCUS READING LOGIC BELOW ---

  // Pre-reserve capacity to prevent mid-word heap reallocations.
  size_t maxPossibleNewTokens = word.length();
  size_t requiredSize = words.size() + maxPossibleNewTokens;

  if (words.capacity() < requiredSize) {
    // Emulate standard geometric growth (doubling) to ensure we don't reallocate on every word.
    size_t newCapacity = words.capacity() * 2;

    // Ensure the doubled capacity is actually enough for this specific word
    if (newCapacity < requiredSize) {
      newCapacity = requiredSize;
    }
    // Set a sensible minimum starting size so the first few words don't trigger tiny reallocations
    if (newCapacity < 16) {
      newCapacity = 16;
    }

    words.reserve(newCapacity);
    wordStyles.reserve(newCapacity);
    wordContinues.reserve(newCapacity);
    wordNoSpaceBefore.reserve(newCapacity);
    wordFocusBoundary.reserve(newCapacity);
    wordGuideDotBefore.reserve(newCapacity);
  }

  // Lambda helper to process and push individual sub-segments of the string
  // Use std::string_view to avoid heap allocations when slicing
  auto processSegment = [&](std::string_view segment, bool isWord, bool attach, bool noSpaceBefore) {
    // The word stays one token; a focus-split word carries its bold-prefix byte boundary
    // (rendered bold up to the boundary) instead of being split into two transient tokens.
    const FocusTokenMetadata meta =
        isWord ? computeFocusMetadata(segment, baseStyle, this->focusReadingEnabled) : FocusTokenMetadata{baseStyle, 0};
    words.emplace_back(segment);
    wordStyles.push_back(meta.style);
    wordContinues.push_back(attach);
    wordNoSpaceBefore.push_back(noSpaceBefore);
    wordFocusBoundary.push_back(meta.boundary);
    wordGuideDotBefore.push_back(guideDotBeforeNextToken);
    guideDotBeforeNextToken = false;
  };

  // Tokenize the string by alternating states (Word vs. Non-Word)
  const unsigned char* ptr = reinterpret_cast<const unsigned char*>(word.c_str());
  const unsigned char* end = ptr + word.length();

  const unsigned char* segmentStart = ptr;
  uint32_t firstCp = utf8NextCodepoint(&ptr);  // Consume the first char to determine initial state
  bool inWordSegment = isWordCharacter(firstCp);

  bool isFirstSegment = true;

  while (ptr < end) {
    const unsigned char* currentCpStart = ptr;
    uint32_t cp = utf8NextCodepoint(&ptr);
    bool isWordChar = isWordCharacter(cp);

    // Whenever the character type flips, slice off the segment we just completed and process it
    if (isWordChar != inWordSegment) {
      size_t segmentLen = currentCpStart - segmentStart;
      std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);

      // Only the very first segment inherits the original attachToPrevious flag.
      // Every subsequent segment MUST attach=true so it glues seamlessly to the prefix.
      processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                     isFirstSegment ? effectiveNoSpaceBefore : false);

      // Setup for the next segment
      segmentStart = currentCpStart;
      inWordSegment = isWordChar;
      isFirstSegment = false;
    }
  }

  // Process the final remaining segment
  size_t segmentLen = end - segmentStart;
  std::string_view segment(reinterpret_cast<const char*>(segmentStart), segmentLen);
  processSegment(segment, inWordSegment, isFirstSegment ? effectiveAttachToPrevious : true,
                 isFirstSegment ? effectiveNoSpaceBefore : false);
  if (wordStartsRtl) {
    hasRtlWord = true;
  }
}

int ParsedText::wordSpacingDeltaPx(const GfxRenderer& renderer, const int fontId) const {
  // wordSpacing is a 10%-step count with 3 == 0%. The delta is that percentage of
  // the regular space width, applied to every real inter-word gap. Signed: values
  // below 3 tighten the spacing, above 3 loosen it.
  const int steps = static_cast<int>(wordSpacing) - 3;  // -3..+30 -> -30%..+300%
  if (steps == 0) return 0;
  return steps * static_cast<int>(renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR)) / 10;
}

int ParsedText::resolveFirstLineIndent(const bool isFirstLine, const GfxRenderer& renderer, const int fontId) const {
  if (!isFirstLine || !isNaturalAlign) {
    return 0;
  }
  // Explicit percentage indent (>= 0, pre-computed from the setting + viewport)
  // overrides the book/CSS behavior: 0 px = flush with the other lines.
  if (firstLineIndentPx >= 0) {
    return firstLineIndentPx;
  }
  if (blockStyle.textIndentDefined) {
    if (blockStyle.textIndent < 0 || !extraParagraphSpacing) {
      return blockStyle.textIndent;
    }
    return 0;
  }
  if (!extraParagraphSpacing) {
    return renderer.getSpaceWidth(fontId, EpdFontFamily::REGULAR) * 3;
  }
  return 0;
}
// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  // Per-paragraph RTL auto-detection: only when CSS/HTML didn't explicitly set direction.
  // Explicit dir="ltr" must be respected and not overridden by content heuristic.
  if (!blockStyle.directionDefined && hasRtlWord) {
    // Check the first few words for RTL letter codepoints (no heap allocation).
    const size_t wordsToScan = std::min(words.size(), RTL_PARAGRAPH_PROBE_WORDS);
    for (size_t i = 0; i < wordsToScan; ++i) {
      if (BidiUtils::startsWithRtl(words[i].c_str(), BidiUtils::RTL_PARAGRAPH_PROBE_DEPTH)) {
        blockStyle.isRtl = true;
        break;
      }
    }
  }

  isNaturalAlign =
      blockStyle.alignment == CssTextAlign::Justify ||
      (blockStyle.isRtl ? blockStyle.alignment == CssTextAlign::Right : blockStyle.alignment == CssTextAlign::Left);

  // Ensure SD card font glyph metrics are loaded before measuring word widths.
  // For flash-based fonts isSdCardFont() returns false and this block is skipped
  // entirely — no heap allocation. For SD card fonts this reads glyph metadata
  // (advanceX only, no bitmaps) for all unique codepoints in this paragraph so
  // that calculateWordWidths() can measure text without on-demand SD I/O.
  if (renderer.isSdCardFont(fontId)) {
    // Style mask: only ask the SD font to load advances for styles actually
    // used in this paragraph. Style index is the low two bits (regular/bold/
    // italic/bold-italic); the underline bit is irrelevant to advance metrics.
    uint8_t styleMask = 0;
    for (auto s : wordStyles) {
      styleMask |= static_cast<uint8_t>(1u << (static_cast<uint8_t>(s) & 0x03));
    }
    if (styleMask == 0) styleMask = 0x01;  // defensive: regular only
    renderer.ensureSdCardFontReady(fontId, words, hyphenationEnabled, styleMask);
    // The guide dot renders in the regular style; load its glyph metrics too so
    // gap measurement and drawing never trigger on-demand SD I/O for it.
    if (guideReadingEnabled) {
      renderer.ensureSdCardFontReady(fontId, GUIDE_DOT_UTF8, 0x01);
    }
  }

  const int pageWidth = viewportWidth;

  // Per-paragraph scratch arena for the line-breaking pass. A 4 KiB initial slab
  // covers typical paragraphs; the arena chains more slabs for very long ones,
  // and everything is freed when this function returns -- one alloc/free instead
  // of several churning std::vector allocations per paragraph. On arena OOM the
  // paragraph is dropped (logged) rather than aborting, matching how extractLine
  // already drops a line when the TextBlock arena cannot be allocated.
  Arena scratch;
  if (!scratch.init(4 * 1024)) {
    LOG_ERR("PTX", "Dropping paragraph: layout scratch arena OOM");
    return;
  }

  ArenaVector<uint16_t> wordWidths(scratch);
  if (!calculateWordWidths(wordWidths, renderer, fontId)) {
    LOG_ERR("PTX", "Dropping paragraph: word-width scratch OOM");
    return;
  }

  ArenaVector<size_t> lineBreakIndices(scratch);
  bool breaksOk;
  if (hyphenationEnabled) {
    // Use greedy layout that can split words mid-loop when a hyphenated prefix fits.
    breaksOk = computeHyphenatedLineBreaks(renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore,
                                           lineBreakIndices);
  } else {
    breaksOk = computeLineBreaks(scratch, renderer, fontId, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore,
                                 lineBreakIndices);
  }
  if (!breaksOk) {
    LOG_ERR("PTX", "Dropping paragraph: line-break scratch OOM");
    return;
  }
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, wordWidths, wordContinues, wordNoSpaceBefore, lineBreakIndices, processLine, renderer,
                fontId);
  }

  // Remove consumed words so size() reflects only remaining words
  if (lineCount > 0) {
    const size_t consumed = lineBreakIndices[lineCount - 1];
    words.erase(words.begin(), words.begin() + consumed);
    wordStyles.erase(wordStyles.begin(), wordStyles.begin() + consumed);
    wordContinues.erase(wordContinues.begin(), wordContinues.begin() + consumed);
    wordNoSpaceBefore.erase(wordNoSpaceBefore.begin(), wordNoSpaceBefore.begin() + consumed);
    wordFocusBoundary.erase(wordFocusBoundary.begin(), wordFocusBoundary.begin() + consumed);
    wordGuideDotBefore.erase(wordGuideDotBefore.begin(), wordGuideDotBefore.begin() + consumed);
  }
}

bool ParsedText::calculateWordWidths(ArenaVector<uint16_t>& wordWidths, const GfxRenderer& renderer, const int fontId) {
  if (!wordWidths.reserve(words.size())) {
    return false;
  }
  for (size_t i = 0; i < words.size(); ++i) {
    if (!wordWidths.push_back(measureTokenWidth(renderer, fontId, words[i], wordStyles[i], wordFocusBoundary[i]))) {
      return false;
    }
  }
  return true;
}

bool ParsedText::computeLineBreaks(Arena& scratch, const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                   ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                   std::vector<bool>& noSpaceBeforeVec, ArenaVector<size_t>& lineBreakIndices) {
  lineBreakIndices.clear();
  if (words.empty()) {
    return true;
  }

  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);
  const int wsDelta = wordSpacingDeltaPx(renderer, fontId);
  // Guide dots widen inter-word gaps. This first implementation applies them only
  // to pure left-to-right paragraphs (no RTL words); RTL/BiDi lines skip dots so
  // the reordered layout stays correct. Gated the same way in extractLine.
  const bool guideOk = !blockStyle.isRtl && !hasRtlWord;

  // Ensure any word that would overflow even as the first entry on a line is split using fallback hyphenation.
  for (size_t i = 0; i < wordWidths.size(); ++i) {
    // First word needs to fit in reduced width if there's an indent
    const int effectiveWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;
    while (wordWidths[i] > effectiveWidth) {
      if (!hyphenateWordAtIndex(i, effectiveWidth, renderer, fontId, wordWidths, /*allowFallbackBreaks=*/true)) {
        break;
      }
    }
  }

  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i.
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting
  // at 'i'. Both are arena-backed; resize() zero-fills, matching the old
  // std::vector value-initialization.
  ArenaVector<int> dp(scratch);
  ArenaVector<size_t> ans(scratch);
  if (!dp.resize(totalWordCount) || !ans.resize(totalWordCount)) {
    return false;
  }

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = 0;
    dp[i] = MAX_COST;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = i == 0 ? pageWidth - firstLineIndent : pageWidth;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Add space before word j, unless it's the first word on the line or a continuation
      int gap = 0;
      if (j > static_cast<size_t>(i) && noSpaceBeforeVec[j]) {
        gap = 0;
      } else if (j > static_cast<size_t>(i) && !continuesVec[j]) {
        gap = (guideOk && wordGuideDotBefore[j])
                  ? guideDotNaturalGap(renderer, fontId, words[j - 1], words[j], wordStyles[j - 1])
                  : renderer.getSpaceAdvance(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]),
                                             wordStyles[j - 1]) +
                        wsDelta;
      } else if (j > static_cast<size_t>(i) && continuesVec[j]) {
        // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
        gap = renderer.getKerning(fontId, lastCodepoint(words[j - 1]), firstCodepoint(words[j]), wordStyles[j - 1]);
      }
      currlen += wordWidths[j] + gap;

      if (currlen > effectivePageWidth) {
        break;
      }

      // Cannot break after word j if the next word attaches to it (continuation group)
      if (j + 1 < totalWordCount && continuesVec[j + 1]) {
        continue;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = effectivePageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    if (!lineBreakIndices.push_back(nextBreakIndex)) {
      return false;
    }
    currentWordIndex = nextBreakIndex;
  }

  return true;
}

// Builds break indices while opportunistically splitting the word that would overflow the current line.
bool ParsedText::computeHyphenatedLineBreaks(const GfxRenderer& renderer, const int fontId, const int pageWidth,
                                             ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                             std::vector<bool>& noSpaceBeforeVec,
                                             ArenaVector<size_t>& lineBreakIndices) {
  lineBreakIndices.clear();
  const int firstLineIndent = resolveFirstLineIndent(true, renderer, fontId);
  const int wsDelta = wordSpacingDeltaPx(renderer, fontId);
  const bool guideOk = !blockStyle.isRtl && !hasRtlWord;

  size_t currentIndex = 0;
  bool isFirstLine = true;

  while (currentIndex < wordWidths.size()) {
    const size_t lineStart = currentIndex;
    int lineWidth = 0;

    // First line has reduced width due to text-indent
    const int effectivePageWidth = isFirstLine ? pageWidth - firstLineIndent : pageWidth;

    // Consume as many words as possible for current line, splitting when prefixes fit
    while (currentIndex < wordWidths.size()) {
      const bool isFirstWord = currentIndex == lineStart;
      int spacing = 0;
      if (!isFirstWord && noSpaceBeforeVec[currentIndex]) {
        spacing = 0;
      } else if (!isFirstWord && !continuesVec[currentIndex]) {
        spacing = (guideOk && wordGuideDotBefore[currentIndex])
                      ? guideDotNaturalGap(renderer, fontId, words[currentIndex - 1], words[currentIndex],
                                           wordStyles[currentIndex - 1])
                      : renderer.getSpaceAdvance(fontId, lastCodepoint(words[currentIndex - 1]),
                                                 firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]) +
                            wsDelta;
      } else if (!isFirstWord && continuesVec[currentIndex]) {
        // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
        spacing = renderer.getKerning(fontId, lastCodepoint(words[currentIndex - 1]),
                                      firstCodepoint(words[currentIndex]), wordStyles[currentIndex - 1]);
      }
      const int candidateWidth = spacing + wordWidths[currentIndex];

      // Word fits on current line
      if (lineWidth + candidateWidth <= effectivePageWidth) {
        lineWidth += candidateWidth;
        ++currentIndex;
        continue;
      }

      // Word would overflow — try to split based on hyphenation points
      const int availableWidth = effectivePageWidth - lineWidth - spacing;
      const bool allowFallbackBreaks = isFirstWord;  // Only for first word on line

      if (availableWidth > 0 &&
          hyphenateWordAtIndex(currentIndex, availableWidth, renderer, fontId, wordWidths, allowFallbackBreaks)) {
        // Prefix now fits; append it to this line and move to next line
        lineWidth += spacing + wordWidths[currentIndex];
        ++currentIndex;
        break;
      }

      // Could not split: force at least one word per line to avoid infinite loop
      if (currentIndex == lineStart) {
        lineWidth += candidateWidth;
        ++currentIndex;
      }
      break;
    }

    // Don't break before a continuation word (e.g., orphaned "?" after "question").
    // Backtrack to the start of the continuation group so the whole group moves to the next line.
    while (currentIndex > lineStart + 1 && currentIndex < wordWidths.size() && continuesVec[currentIndex]) {
      --currentIndex;
    }

    if (!lineBreakIndices.push_back(currentIndex)) {
      return false;
    }
    isFirstLine = false;
  }

  return true;
}

// Splits words[wordIndex] into prefix (adding a hyphen only when needed) and remainder when a legal breakpoint fits the
// available width.
bool ParsedText::hyphenateWordAtIndex(const size_t wordIndex, const int availableWidth, const GfxRenderer& renderer,
                                      const int fontId, ArenaVector<uint16_t>& wordWidths,
                                      const bool allowFallbackBreaks) {
  // Guard against invalid indices or zero available width before attempting to split.
  if (availableWidth <= 0 || wordIndex >= words.size()) {
    return false;
  }

  const std::string& word = words[wordIndex];
  const auto style = wordStyles[wordIndex];
  const uint8_t origBoundary = wordFocusBoundary[wordIndex];

  // Collect candidate breakpoints (byte offsets and hyphen requirements).
  auto breakInfos = Hyphenator::breakOffsets(word, allowFallbackBreaks);
  if (breakInfos.empty()) {
    return false;
  }

  size_t chosenOffset = 0;
  int chosenWidth = -1;
  bool chosenNeedsHyphen = true;

  // Iterate over each legal breakpoint and retain the widest prefix that still fits.
  for (const auto& info : breakInfos) {
    const size_t offset = info.byteOffset;
    if (offset == 0 || offset >= word.size()) {
      continue;
    }

    const bool needsHyphen = info.requiresInsertedHyphen;
    // The prefix carries the original bold boundary if the break is past it; otherwise the
    // whole prefix is bold. Measured as a focus token so the fit check matches what renders.
    const uint8_t pb = (origBoundary != 0 && origBoundary <= offset) ? origBoundary : 0;
    const EpdFontFamily::Style ps = (origBoundary > 0 && origBoundary > offset)
                                        ? static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD)
                                        : style;
    const int prefixWidth = measureTokenWidth(renderer, fontId, word.substr(0, offset), ps, pb, needsHyphen);
    if (prefixWidth > availableWidth || prefixWidth <= chosenWidth) {
      continue;  // Skip if too wide or not an improvement
    }

    chosenWidth = prefixWidth;
    chosenOffset = offset;
    chosenNeedsHyphen = needsHyphen;
  }

  if (chosenWidth < 0) {
    // No hyphenation point produced a prefix that fits in the remaining space.
    return false;
  }

  // Split the word at the selected breakpoint and append a hyphen if required.
  std::string remainder = word.substr(chosenOffset);

  // Carry the original bold boundary across the split instead of recomputing it from the
  // fragment: a break past the bold region leaves the prefix's boundary intact and the
  // remainder fully regular; a break inside it makes the whole prefix bold and shifts the
  // leftover bold bytes onto the remainder. Preserves the pre-split rendered bolding.
  uint8_t prefixBoundary = 0;
  uint8_t remainderBoundary = 0;
  EpdFontFamily::Style prefixStyle = style;
  if (origBoundary > 0) {
    if (origBoundary <= chosenOffset) {
      prefixBoundary = origBoundary;
    } else {
      prefixStyle = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::BOLD);
      remainderBoundary = static_cast<uint8_t>(origBoundary - chosenOffset);
    }
  }

  // Do the one fallible (arena-backed) mutation first: an OOM here returns false
  // ("no split") with the token vectors and the width array still consistent, so
  // the caller simply leaves the word whole rather than aborting on a desync.
  const uint16_t remainderWidth = measureTokenWidth(renderer, fontId, remainder, style, remainderBoundary);
  if (!wordWidths.insert(wordIndex + 1, remainderWidth)) {
    return false;
  }
  wordWidths[wordIndex] = static_cast<uint16_t>(chosenWidth);

  words[wordIndex].resize(chosenOffset);
  if (chosenNeedsHyphen) {
    words[wordIndex].push_back('-');
  }
  wordStyles[wordIndex] = prefixStyle;
  wordFocusBoundary[wordIndex] = prefixBoundary;

  // Insert the remainder word (with matching style and continuation flag) directly after the prefix.
  words.insert(words.begin() + wordIndex + 1, remainder);
  wordStyles.insert(wordStyles.begin() + wordIndex + 1, style);
  // The remainder carries any bold bytes that spilled past the break; it carries no guide
  // dot - it starts fresh on the next line.
  wordFocusBoundary.insert(wordFocusBoundary.begin() + wordIndex + 1, remainderBoundary);
  wordGuideDotBefore.insert(wordGuideDotBefore.begin() + wordIndex + 1, false);

  // Continuation flag handling after splitting a word into prefix + remainder.
  //
  // The prefix keeps the original word's continuation flag so that no-break-space groups
  // stay linked. The remainder always gets continues=false because it starts on the next
  // line and is not attached to the prefix.
  //
  // Example: "200&#xA0;Quadratkilometer" produces tokens:
  //   [0] "200"               continues=false
  //   [1] " "                 continues=true
  //   [2] "Quadratkilometer"  continues=true   <-- the word being split
  //
  // After splitting "Quadratkilometer" at "Quadrat-" / "kilometer":
  //   [0] "200"         continues=false
  //   [1] " "           continues=true
  //   [2] "Quadrat-"    continues=true   (KEPT — still attached to the no-break group)
  //   [3] "kilometer"   continues=false  (NEW — starts fresh on the next line)
  //
  // This lets the backtracking loop keep the entire prefix group ("200 Quadrat-") on one
  // line, while "kilometer" moves to the next line.
  // wordContinues[wordIndex] is intentionally left unchanged — the prefix keeps its original attachment.
  wordContinues.insert(wordContinues.begin() + wordIndex + 1, false);
  wordNoSpaceBefore.insert(wordNoSpaceBefore.begin() + wordIndex + 1, false);

  return true;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const ArenaVector<uint16_t>& wordWidths,
                             const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                             const ArenaVector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             const GfxRenderer& renderer, const int fontId) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  const int firstLineIndent = resolveFirstLineIndent(breakIndex == 0, renderer, fontId);

  // Guide dots apply only to pure LTR lines (matches the gate in computeLineBreaks).
  // A dot sits in the gap BEFORE a word, so the first word on a line never carries
  // one (k > 0). guideOk is mutually exclusive with visual reordering, so the RTL
  // and reordered layout branches below never need to account for dots.
  const bool guideOk = !blockStyle.isRtl && !hasRtlWord;
  const auto guideDotBeforeLine = [&](const size_t k) {
    return guideOk && k > 0 && wordGuideDotBefore[lastBreakAt + k];
  };

  // Build line data by moving from the original vectors using index range. These
  // are reused member buffers: clear() keeps their capacity so the same storage
  // serves every line on the page instead of a fresh allocation per line.
  auto& lineWords = lineWordsScratch;
  auto& lineWordStyles = lineStylesScratch;
  lineWords.clear();
  lineWordStyles.clear();
  lineWords.reserve(lineWordCount);
  lineWordStyles.reserve(lineWordCount);

  for (size_t i = 0; i < lineWordCount; ++i) {
    std::string word = std::move(words[lastBreakAt + i]);
    if (containsSoftHyphen(word)) {
      stripSoftHyphensInPlace(word);
    }
    lineWords.push_back(std::move(word));
    lineWordStyles.push_back(wordStyles[lastBreakAt + i]);
  }

  // Word-spacing delta applied to every real inter-word gap (0 at the default).
  const int wsDelta = wordSpacingDeltaPx(renderer, fontId);

  // Calculate total word width for this line, count actual word gaps,
  // and accumulate total natural gap widths (including space kerning adjustments).
  int lineWordWidthSum = 0;
  size_t actualGapCount = 0;
  int totalNaturalGaps = 0;

  for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
    lineWordWidthSum += wordWidths[lastBreakAt + wordIdx];
    // Count gaps: each word after the first creates a gap, unless it's a continuation
    if (wordIdx > 0 && noSpaceBeforeVec[lastBreakAt + wordIdx]) {
      // Unicode break opportunity with no inserted Latin-style space. It is still
      // a stretchable gap for justified CJK/Korean text.
      actualGapCount++;
    } else if (wordIdx > 0 && !continuesVec[lastBreakAt + wordIdx]) {
      actualGapCount++;
      totalNaturalGaps +=
          guideDotBeforeLine(wordIdx)
              ? guideDotNaturalGap(renderer, fontId, lineWords[wordIdx - 1], lineWords[wordIdx],
                                   lineWordStyles[wordIdx - 1])
              : renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx - 1]),
                                         firstCodepoint(lineWords[wordIdx]), lineWordStyles[wordIdx - 1]) +
                    wsDelta;
    } else if (wordIdx > 0 && continuesVec[lastBreakAt + wordIdx]) {
      // Non-breaking space tokens (" " with continues=true) are visible, stretchable spaces —
      // count them as justifiable gaps so justifyExtra is distributed to them too.
      if (lineWords[wordIdx] == " ") {
        actualGapCount++;
      }
      // Cross-boundary kerning for continuation words (e.g. nonbreaking spaces, attached punctuation)
      totalNaturalGaps += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx - 1]),
                                              firstCodepoint(lineWords[wordIdx]), lineWordStyles[wordIdx - 1]);
    }
  }

  // Calculate spacing (account for indent reducing effective page width on first line)
  const int effectivePageWidth = pageWidth - firstLineIndent;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  // For RTL, implicit/default Left alignment becomes Right alignment.
  // Explicit text-align:left must remain left for CSS correctness.
  const CssTextAlign effectiveAlignment =
      (blockStyle.isRtl && !blockStyle.textAlignDefined && blockStyle.alignment == CssTextAlign::Left)
          ? CssTextAlign::Right
          : blockStyle.alignment;

  // For justified text, compute per-gap extra to distribute remaining space evenly
  const int spareSpace = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
  const int justifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                               ? computeJustifyExtra(spareSpace, actualGapCount)
                               : 0;

  // BiDi processing: reorder words with UAX#9 in full-line context.
  visualOrderScratch.clear();
  visualOrderScratch.reserve(lineWordCount);
  // Skip expensive visual-order resolution for pure LTR paragraphs that have no RTL words.
  const bool shouldResolveVisualOrder = blockStyle.isRtl || hasRtlWord;
  const bool willReorder =
      shouldResolveVisualOrder && BidiUtils::computeVisualWordOrder(lineWords, blockStyle.isRtl, visualOrderScratch);

  auto& lineXPos = lineXPosScratch;
  lineXPos.clear();
  lineXPos.reserve(lineWordCount);

  if (willReorder) {
    reorderedWordsScratch.clear();
    reorderedStylesScratch.clear();
    reorderedWidthsScratch.clear();
    reorderedContinuesScratch.clear();
    reorderedNoSpaceBeforeScratch.clear();
    reorderedFocusBoundaryScratch.clear();
    reorderedWordsScratch.reserve(visualOrderScratch.size());
    reorderedStylesScratch.reserve(visualOrderScratch.size());
    reorderedWidthsScratch.reserve(visualOrderScratch.size());
    reorderedContinuesScratch.reserve(visualOrderScratch.size());
    reorderedNoSpaceBeforeScratch.reserve(visualOrderScratch.size());
    reorderedFocusBoundaryScratch.reserve(visualOrderScratch.size());

    for (size_t i = 0; i < visualOrderScratch.size(); ++i) {
      const uint16_t src = visualOrderScratch[i];
      reorderedWordsScratch.push_back(std::move(lineWords[src]));
      reorderedStylesScratch.push_back(lineWordStyles[src]);
      reorderedWidthsScratch.push_back(wordWidths[lastBreakAt + src]);
      reorderedFocusBoundaryScratch.push_back(wordFocusBoundary[lastBreakAt + src]);

      // Continuation means "no break/gap between two adjacent logical tokens".
      // After visual reordering (common in RTL), an adjacent logical pair can appear
      // as either (prev -> curr) or (curr -> prev) in visual order; preserve both.
      bool continues = false;
      if (i > 0) {
        const size_t prevSrc = visualOrderScratch[i - 1];
        const size_t currSrc = src;
        const bool forwardAdjacent = currSrc == prevSrc + 1;
        const bool reverseAdjacent = prevSrc == currSrc + 1;

        if (forwardAdjacent && continuesVec[lastBreakAt + currSrc]) {
          continues = true;
        } else if (reverseAdjacent && continuesVec[lastBreakAt + prevSrc]) {
          continues = true;
        }
      }
      reorderedContinuesScratch.push_back(continues);
      reorderedNoSpaceBeforeScratch.push_back(!continues && noSpaceBeforeVec[lastBreakAt + src]);
    }

    int reorderedWordWidthSum = 0;
    size_t reorderedGapCount = 0;
    int reorderedNaturalGaps = 0;
    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      reorderedWordWidthSum += reorderedWidthsScratch[wordIdx];
      if (wordIdx > 0 && reorderedNoSpaceBeforeScratch[wordIdx]) {
        // Unicode break opportunity with no inserted Latin-style space. It is still
        // a stretchable gap for justified CJK/Korean text.
        reorderedGapCount++;
      } else if (wordIdx > 0 && !reorderedContinuesScratch[wordIdx]) {
        reorderedGapCount++;
        reorderedNaturalGaps += renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                                         firstCodepoint(reorderedWordsScratch[wordIdx]),
                                                         reorderedStylesScratch[wordIdx - 1]) +
                                wsDelta;
      } else if (wordIdx > 0 && reorderedContinuesScratch[wordIdx]) {
        if (reorderedWordsScratch[wordIdx] == " ") {
          reorderedGapCount++;
        }
        reorderedNaturalGaps +=
            renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx - 1]),
                                firstCodepoint(reorderedWordsScratch[wordIdx]), reorderedStylesScratch[wordIdx - 1]);
      }
    }

    const int reorderedSpare = effectivePageWidth - reorderedWordWidthSum - reorderedNaturalGaps;
    const int reorderedJustifyExtra = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                          ? computeJustifyExtra(reorderedSpare, reorderedGapCount)
                                          : 0;

    const int justifyContribution = (effectiveAlignment == CssTextAlign::Justify && !isLastLine)
                                        ? reorderedJustifyExtra * static_cast<int>(reorderedGapCount)
                                        : 0;
    const int contentWidth = reorderedWordWidthSum + reorderedNaturalGaps + justifyContribution;

    int xpos = 0;
    if (blockStyle.isRtl) {
      if (effectiveAlignment == CssTextAlign::Right || effectiveAlignment == CssTextAlign::Justify) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    } else {
      xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - contentWidth;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - contentWidth) / 2;
      }
    }

    for (size_t wordIdx = 0; wordIdx < reorderedWidthsScratch.size(); wordIdx++) {
      lineXPos.push_back(static_cast<int16_t>(xpos));
      xpos += reorderedWidthsScratch[wordIdx];

      const bool nextIsContinuation =
          wordIdx + 1 < reorderedWidthsScratch.size() && reorderedContinuesScratch[wordIdx + 1];
      if (nextIsContinuation) {
        int advance =
            renderer.getKerning(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                firstCodepoint(reorderedWordsScratch[wordIdx + 1]), reorderedStylesScratch[wordIdx]);
        // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
        // no-break space must not receive justifyExtra, or the line over-stretches by one
        // gap and the last word is pushed past the right margin (issue #2185).
        if (wordIdx > 0 && reorderedWordsScratch[wordIdx] == " " && reorderedContinuesScratch[wordIdx] &&
            effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          advance += reorderedJustifyExtra;
        }
        xpos += advance;
      } else if (wordIdx + 1 < reorderedWidthsScratch.size()) {
        const bool nextNoSpace = reorderedNoSpaceBeforeScratch[wordIdx + 1];
        int gap = nextNoSpace ? 0
                              : renderer.getSpaceAdvance(fontId, lastCodepoint(reorderedWordsScratch[wordIdx]),
                                                         firstCodepoint(reorderedWordsScratch[wordIdx + 1]),
                                                         reorderedStylesScratch[wordIdx]) +
                                    wsDelta;
        if (effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
          gap += reorderedJustifyExtra;
        }
        xpos += gap;
      }
    }

    lineWords.swap(reorderedWordsScratch);
    lineWordStyles.swap(reorderedStylesScratch);
  } else {
    // Standard LTR/RTL positioning loop when no visual reordering is needed
    if (blockStyle.isRtl) {
      // RTL: position words from right to left
      int xpos = effectivePageWidth;
      if (effectiveAlignment == CssTextAlign::Left) {
        // Explicit left alignment in RTL context
        xpos = lineWordWidthSum + totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth + lineWordWidthSum + totalNaturalGaps) / 2;
      }
      // For Right and Justify, start from right edge (xpos = effectivePageWidth)

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        xpos -= wordWidths[lastBreakAt + wordIdx];
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          // Cross-boundary kerning for continuation words
          int advance = renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]),
                                            firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          // wordIdx > 0: see the LTR branch — a leading no-break space is not a justifiable gap.
          if (wordIdx > 0 && lineWords[wordIdx] == " " && continuesVec[lastBreakAt + wordIdx] &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            advance += justifyExtra;
          }
          xpos -= advance;
        } else {
          int gap = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = noSpaceBeforeVec[lastBreakAt + wordIdx + 1];
            gap = nextNoSpace
                      ? 0
                      : renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx]),
                                                 firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]) +
                            wsDelta;
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            gap += justifyExtra;
          }
          xpos -= gap;
        }
      }
    } else {
      // LTR: position words from left to right
      int xpos = firstLineIndent;
      if (effectiveAlignment == CssTextAlign::Right) {
        xpos = effectivePageWidth - lineWordWidthSum - totalNaturalGaps;
      } else if (effectiveAlignment == CssTextAlign::Center) {
        xpos = (effectivePageWidth - lineWordWidthSum - totalNaturalGaps) / 2;
      }

      for (size_t wordIdx = 0; wordIdx < lineWordCount; wordIdx++) {
        lineXPos.push_back(static_cast<int16_t>(xpos));

        const bool nextIsContinuation = wordIdx + 1 < lineWordCount && continuesVec[lastBreakAt + wordIdx + 1];
        if (nextIsContinuation) {
          int advance = wordWidths[lastBreakAt + wordIdx];
          advance += renderer.getKerning(fontId, lastCodepoint(lineWords[wordIdx]),
                                         firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]);
          // wordIdx > 0 mirrors the gap accounting above (which skips index 0): a leading
          // no-break space must not receive justifyExtra, or the line over-stretches by one
          // gap and the last word is pushed past the right margin (issue #2185).
          if (wordIdx > 0 && lineWords[wordIdx] == " " && continuesVec[lastBreakAt + wordIdx] &&
              effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            advance += justifyExtra;
          }
          xpos += advance;
        } else {
          int gap = 0;
          bool nextNoSpace = false;
          if (wordIdx + 1 < lineWordCount) {
            nextNoSpace = noSpaceBeforeVec[lastBreakAt + wordIdx + 1];
            gap = nextNoSpace ? 0
                  : guideDotBeforeLine(wordIdx + 1)
                      ? guideDotNaturalGap(renderer, fontId, lineWords[wordIdx], lineWords[wordIdx + 1],
                                           lineWordStyles[wordIdx])
                      : renderer.getSpaceAdvance(fontId, lastCodepoint(lineWords[wordIdx]),
                                                 firstCodepoint(lineWords[wordIdx + 1]), lineWordStyles[wordIdx]) +
                            wsDelta;
          }
          if (wordIdx + 1 < lineWordCount && effectiveAlignment == CssTextAlign::Justify && !isLastLine) {
            gap += justifyExtra;
          }
          xpos += wordWidths[lastBreakAt + wordIdx] + gap;
        }
      }
    }
  }

  const auto focusBoundaryAt = [&](const size_t idx) -> uint8_t {
    return willReorder ? reorderedFocusBoundaryScratch[idx] : wordFocusBoundary[lastBreakAt + idx];
  };

  // Fast path: when no word on this line is focus-split and no guide dot sits between
  // words, skip the per-word annotation work and pass empty vectors. TextBlock pays zero
  // per-word RAM cost for these when the vectors are empty.
  bool lineHasFocusSplit = false;
  bool lineHasGuideDot = false;
  for (size_t i = 0; i < lineWordCount; i++) {
    if (focusBoundaryAt(i) > 0) {
      lineHasFocusSplit = true;
    }
    if (guideDotBeforeLine(i)) {
      lineHasGuideDot = true;
    }
    if (lineHasFocusSplit && lineHasGuideDot) {
      break;
    }
  }

  if (!lineHasFocusSplit && !lineHasGuideDot) {
    // TextBlock flattens the vectors into its arena; they stay owned here and die at return.
    auto block = std::make_shared<TextBlock>(lineWords, lineXPos, lineWordStyles, std::vector<uint8_t>{},
                                             std::vector<uint16_t>{}, std::vector<uint16_t>{}, blockStyle);
    if (!block->valid()) {
      LOG_ERR("PTX", "Dropping line: TextBlock arena allocation failed");
      return;
    }
    processLine(std::move(block));
    return;
  }

  // Annotation path: each word is one output entry carrying its focus boundary; guide-dot
  // offsets are recorded per output word. Focus and guide annotations are filled only when
  // present on this line, so a line with just one of them does not pay per-word RAM for the other.
  auto& outWords = outWordsScratch;
  auto& outXPos = outXPosScratch;
  auto& outStyles = outStylesScratch;
  auto& outBoundaries = outBoundaryScratch;
  auto& outSuffixX = outSuffixXScratch;
  auto& outGuideDotXOffset = outGuideDotXOffsetScratch;
  outWords.clear();
  outXPos.clear();
  outStyles.clear();
  outBoundaries.clear();
  outSuffixX.clear();
  outGuideDotXOffset.clear();
  outWords.reserve(lineWordCount);
  outXPos.reserve(lineWordCount);
  outStyles.reserve(lineWordCount);
  if (lineHasFocusSplit) {
    outBoundaries.reserve(lineWordCount);
    outSuffixX.reserve(lineWordCount);
  }
  if (lineHasGuideDot) {
    outGuideDotXOffset.reserve(lineWordCount);
  }

  const int dotAdv = lineHasGuideDot ? guideDotAdvance(renderer, fontId) : 0;

  for (size_t i = 0; i < lineWordCount; i++) {
    uint8_t boundary = focusBoundaryAt(i);
    if (boundary >= lineWords[i].size()) {
      boundary = 0;
    }
    outWords.push_back(std::move(lineWords[i]));
    outXPos.push_back(lineXPos[i]);
    // The style is already regular for a focus-split word; the boundary drives the bold
    // prefix at render time, so nothing is stripped here.
    outStyles.push_back(lineWordStyles[i]);
    if (lineHasFocusSplit) {
      outBoundaries.push_back(boundary);
      outSuffixX.push_back(static_cast<uint16_t>(
          std::max(0, measureFocusSuffixX(renderer, fontId, outWords.back(), outStyles.back(), boundary))));
    }
    if (lineHasGuideDot) {
      outGuideDotXOffset.push_back(0);
      // A guide dot precedes this word: centre it in the gap between the previous output
      // word and this one, so it stays mid-gap even when justification has stretched the
      // gap. The offset is stored on the previous output word, which render() draws from.
      if (guideDotBeforeLine(i) && outGuideDotXOffset.size() >= 2) {
        const int prevX = static_cast<int>(outXPos[outXPos.size() - 2]);
        const int prevWidth =
            measureWordWidth(renderer, fontId, outWords[outWords.size() - 2], outStyles[outStyles.size() - 2]);
        const int gapStart = prevX + prevWidth;
        const int gapWidth = static_cast<int>(lineXPos[i]) - gapStart;
        const int dotX = gapStart + (gapWidth - dotAdv) / 2;
        const int dotDelta = dotX - prevX;
        outGuideDotXOffset[outGuideDotXOffset.size() - 2] = static_cast<uint16_t>(dotDelta > 0 ? dotDelta : 0);
      }
    }
  }

  auto block = std::make_shared<TextBlock>(outWords, outXPos, outStyles, outBoundaries, outSuffixX, outGuideDotXOffset,
                                           blockStyle);
  if (!block->valid()) {
    LOG_ERR("PTX", "Dropping line: TextBlock arena allocation failed");
    return;
  }
  processLine(std::move(block));
}
