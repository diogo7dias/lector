#include "TextBlock.h"

#include <BidiUtils.h>
#include <GfxRenderer.h>
#include <Logging.h>
#include <Memory.h>
#include <Serialization.h>

#include <cstring>

size_t TextBlock::arenaSize(const uint16_t wordCount, const bool hasFocus, const bool hasGuideDots,
                            const uint16_t textBytes) {
  // Layout documented in TextBlock.h: 16-bit arrays first, then 8-bit arrays, then text.
  size_t size = static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(int16_t) + sizeof(uint8_t));
  if (hasFocus) {
    size += static_cast<size_t>(wordCount) * (sizeof(uint16_t) + sizeof(uint8_t));
  }
  if (hasGuideDots) {
    size += static_cast<size_t>(wordCount) * sizeof(uint16_t);
  }
  return size + textBytes;
}

void TextBlock::bindArenaPointers() {
  uint8_t* base = arena.get();
  const size_t wc = numWords;
  textOffArr = reinterpret_cast<const uint16_t*>(base);
  xposArr = reinterpret_cast<const int16_t*>(base + wc * 2);
  size_t off = wc * 4;
  if (focusPresent) {
    focusSuffixXArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  if (guideDotsPresent) {
    guideDotXOffsetArr = reinterpret_cast<const uint16_t*>(base + off);
    off += wc * 2;
  }
  stylesArr = base + off;
  off += wc;
  if (focusPresent) {
    focusBoundaryArr = base + off;
    off += wc;
  }
  textArr = reinterpret_cast<const char*>(base + off);
}

TextBlock::TextBlock(const std::vector<std::string>& words, const std::vector<int16_t>& wordXpos,
                     const std::vector<EpdFontFamily::Style>& wordStyles, const std::vector<uint8_t>& focusBoundary,
                     const std::vector<uint16_t>& focusSuffixX, const std::vector<uint16_t>& guideDotXOffset,
                     const BlockStyle& blockStyle, std::vector<std::string> rubyTexts)
    : blockStyle(blockStyle), rubyTexts(std::move(rubyTexts)) {
  // Same invariant as deserialize(): a block never holds an all-empty rubyTexts, so a
  // ruby-less line costs nothing beyond its arena. The layout engine hands one over for
  // every line it extracts, ruby or not; release it here rather than carrying it for the
  // block's lifetime. Move-assigning an empty vector frees the buffer (clear() would not).
  // Upstream #2772.
  if (!hasRuby()) {
    this->rubyTexts = std::vector<std::string>{};
  }

  // Focus and guide-dot annotations are optional: empty vectors mean no word in
  // this block carries them. When present, each must be sized in lockstep with words[].
  const bool hasFocus = !focusBoundary.empty();
  const bool hasGuideDots = !guideDotXOffset.empty();
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size() || words.size() > 10000 ||
      (hasFocus && (words.size() != focusBoundary.size() || words.size() != focusSuffixX.size())) ||
      (hasGuideDots && words.size() != guideDotXOffset.size())) {
    LOG_ERR("TXB",
            "Construction failed: size mismatch (words=%u, xpos=%u, styles=%u, boundary=%u, suffixX=%u, dots=%u)",
            static_cast<uint32_t>(words.size()), static_cast<uint32_t>(wordXpos.size()),
            static_cast<uint32_t>(wordStyles.size()), static_cast<uint32_t>(focusBoundary.size()),
            static_cast<uint32_t>(focusSuffixX.size()), static_cast<uint32_t>(guideDotXOffset.size()));
    isValid = false;
    return;
  }

  numWords = static_cast<uint16_t>(words.size());
  focusPresent = hasFocus;
  guideDotsPresent = hasGuideDots;
  if (numWords == 0) {
    return;  // valid empty block, no arena
  }

  // Pass 1: total text size, one NUL per word. A line is at most a physical
  // row of the page, so uint16_t offsets are ample; reject anything larger.
  size_t totalText = 0;
  for (const auto& w : words) totalText += w.size() + 1;
  if (totalText > UINT16_MAX) {
    LOG_ERR("TXB", "Construction failed: text size %u exceeds arena limit", static_cast<uint32_t>(totalText));
    numWords = 0;
    focusPresent = false;
    guideDotsPresent = false;
    isValid = false;
    return;
  }
  textBytes = static_cast<uint16_t>(totalText);

  const size_t size = arenaSize(numWords, focusPresent, guideDotsPresent, textBytes);
  arena = makeUniqueNoThrow<uint8_t[]>(size);
  if (!arena) {
    LOG_ERR("TXB", "OOM: arena %u bytes", static_cast<uint32_t>(size));
    numWords = 0;
    textBytes = 0;
    focusPresent = false;
    guideDotsPresent = false;
    isValid = false;
    return;
  }
  bindArenaPointers();

  // Pass 2: fill. Mutable aliases of the const views bound above.
  auto* textOff = const_cast<uint16_t*>(textOffArr);
  auto* xpos = const_cast<int16_t*>(xposArr);
  auto* styles = const_cast<uint8_t*>(stylesArr);
  auto* text = const_cast<char*>(textArr);
  uint16_t off = 0;
  for (uint16_t i = 0; i < numWords; i++) {
    textOff[i] = off;
    xpos[i] = wordXpos[i];
    styles[i] = static_cast<uint8_t>(wordStyles[i]);
    memcpy(text + off, words[i].data(), words[i].size());
    off += static_cast<uint16_t>(words[i].size());
    text[off++] = '\0';
  }
  if (focusPresent) {
    auto* suffixX = const_cast<uint16_t*>(focusSuffixXArr);
    auto* boundary = const_cast<uint8_t*>(focusBoundaryArr);
    for (uint16_t i = 0; i < numWords; i++) {
      suffixX[i] = focusSuffixX[i];
      boundary[i] = focusBoundary[i];
    }
  }
  if (guideDotsPresent) {
    auto* dotX = const_cast<uint16_t*>(guideDotXOffsetArr);
    for (uint16_t i = 0; i < numWords; i++) {
      dotX[i] = guideDotXOffset[i];
    }
  }
}

bool TextBlock::hasRuby() const {
  for (const auto& rt : rubyTexts) {
    if (!rt.empty()) return true;
  }
  return false;
}

void TextBlock::render(const GfxRenderer& renderer, const int sectionFontId, const int x, const int y) const {
  if (!isValid) {
    LOG_ERR("TXB", "Render skipped: invalid block");
    return;
  }
  // A sized variant that can no longer be loaded falls back to the section font.
  const int fontId = renderFontId(renderer, blockFontId(sectionFontId), sectionFontId);

  const bool scanning = renderer.isFontCacheScanning();
  // Words in other sizes share the baseline set by the line's tallest font.
  const int ascender = lineAscent(renderer, sectionFontId);

  // Resolve ruby positions. Layout (extractLine) has already reserved extraStartOffset on the
  // left and extraEndOffset on the right, so the centered rubyX is always within the page margins.
  struct RubyDrawInfo {
    int x;
    std::string text;
    BidiUtils::BidiBaseDir baseDir;
  };
  const bool blockHasRuby = hasRuby();
  std::vector<RubyDrawInfo> rubies;
  if (blockHasRuby) {
    rubies.resize(numWords);
    for (uint16_t i = 0; i < numWords; i++) {
      if (i < rubyTexts.size() && !rubyTexts[i].empty() && (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
        int groupWordCount = 1;
        while (i + groupWordCount < numWords && (wordStyle(i + groupWordCount) & EpdFontFamily::RUBY_CONTINUE) != 0) {
          groupWordCount++;
        }
        int groupActualWidth = 0;
        for (int k = 0; k < groupWordCount; ++k) {
          groupActualWidth += renderer.getTextAdvanceX(fontId, wordText(i + k), wordStyle(i + k));
        }
        const int rubyWidth = renderer.getTextAdvanceX(fontId, rubyTexts[i].c_str(), EpdFontFamily::SUP);
        const int leaderWordX = xposArr[i] + x;
        const auto baseDir = BidiUtils::detectParagraphLevel(
            wordText(i), blockStyle.isRtl ? BidiUtils::BidiBaseDir::RTL : BidiUtils::BidiBaseDir::LTR);
        rubies[i] = {leaderWordX - (rubyWidth - groupActualWidth) / 2, rubyTexts[i], baseDir};
        i += groupWordCount - 1;
      }
    }
  }

  struct DecorationLineTracker {
    EpdFontFamily::Style style;
    int startX = -1;
    int endX = -1;
    int yPos = 0;

    bool active() const { return startX != -1; }
    void reset() {
      startX = -1;
      endX = -1;
      yPos = 0;
    }
  };

  DecorationLineTracker decorationLines[] = {
      {EpdFontFamily::UNDERLINE},
      {EpdFontFamily::STRIKETHROUGH},
  };

  const auto flushDecoration = [&](DecorationLineTracker& line) {
    if (line.active()) {
      renderer.drawLine(line.startX, line.yPos, line.endX, line.yPos, 2, true);
      line.reset();
    }
  };
  const auto flushDecorations = [&]() {
    for (auto& line : decorationLines) {
      flushDecoration(line);
    }
  };

  // Loop-invariant: hoisted out of the word loop so rubyTexts is scanned once,
  // not once per word.
  const int rubyShift = getRubyShift(ascender);

  for (uint16_t i = 0; i < numWords; i++) {
    const char* word = wordText(i);
    const int wordX = xposArr[i] + x;
    const EpdFontFamily::Style currentStyle = wordStyle(i);
    const auto baseDir = BidiUtils::detectParagraphLevel(
        word, blockStyle.isRtl ? BidiUtils::BidiBaseDir::RTL : BidiUtils::BidiBaseDir::LTR);
    const uint8_t boundary = focusBoundary(i);
    const int wordFont = wordFontData ? renderFontId(renderer, wordFontId(i, sectionFontId), sectionFontId) : fontId;
    const int wordAscender = wordFont == fontId && !wordFontData ? ascender : renderer.getFontAscenderSize(wordFont);

    // SUP/SUB shift the baseline passed to drawText; the glyph is also scaled 50% inside
    // drawText, so these offsets are chosen relative to the full-size ascender:
    //   SUP: raise by 40% of ascender — sits clearly above the cap-height
    //   SUB: lower by 25% of ascender — descends below baseline without clashing with ascenders below
    int wordY = y + rubyShift + (ascender - wordAscender);
    if ((currentStyle & EpdFontFamily::SUP) != 0) {
      wordY -= ascender * 2 / 5;
    } else if ((currentStyle & EpdFontFamily::SUB) != 0) {
      wordY += ascender / 4;
    }

    const int drawX = wordX;

    if (boundary > 0) {
      // Focus split: draw bold prefix, then the regular suffix at a pre-computed x offset.
      // The bold prefix is bounded to 9 codepoints by the clamp on targetBoldChars in
      // ParsedText::addWord; 9 UTF-8 codepoints occupy at most 9 * 4 = 36 bytes, +1 for null = 37.
      // suffixX is computed at cache-creation time to avoid font metric lookups at render time.
      static constexpr size_t MAX_FOCUS_PREFIX_BYTES = 9 * 4 + 1;
      char boldBuf[40];
      static_assert(sizeof(boldBuf) >= MAX_FOCUS_PREFIX_BYTES,
                    "boldBuf too small for max focus prefix (9 codepoints * 4 UTF-8 bytes + null)");
      const auto boldStyle = static_cast<EpdFontFamily::Style>(currentStyle | EpdFontFamily::BOLD);
      const size_t boldLen =
          std::min<size_t>({static_cast<size_t>(boundary), static_cast<size_t>(wordTextLen(i)), sizeof(boldBuf) - 1});
      memcpy(boldBuf, word, boldLen);
      boldBuf[boldLen] = '\0';
      renderer.drawText(wordFont, drawX, wordY, boldBuf, true, boldStyle, baseDir);
      const int suffixX = drawX + focusSuffixXArr[i];
      renderer.drawText(wordFont, suffixX, wordY, word + boldLen, true, currentStyle, baseDir);
    } else {
      renderer.drawText(wordFont, drawX, wordY, word, true, currentStyle, baseDir, letterSpacing);
    }

    // Horizontal ruby text rendering
    if (blockHasRuby && i < rubyTexts.size() && !rubyTexts[i].empty() &&
        (wordStyle(i) & EpdFontFamily::RUBY_CONTINUE) == 0) {
      const int rubyY = wordY - ascender;
      renderer.drawText(fontId, rubies[i].x, rubyY, rubies[i].text.c_str(), true, EpdFontFamily::SUP,
                        rubies[i].baseDir);
    }

    // Guide dot: draw the middle dot (U+00B7) in the widened gap before this word.
    // The x offset was pre-computed at layout time; 0 means no dot for this word.
    // Drawn before the scan short-circuit so the dot glyph is registered in the
    // font cache scan pass like every other glyph.
    const uint16_t dotOffset = guideDotXOffset(i);
    if (dotOffset > 0) {
      renderer.drawText(fontId, wordX + dotOffset, wordY, "\xc2\xb7", true, EpdFontFamily::REGULAR, baseDir);
    }

    if (scanning) {
      continue;
    }

    if (EpdFontFamily::hasTextDecoration(currentStyle)) {
      int lineStartX = drawX;
      int lineWidth = renderer.getTextWidth(wordFont, word, currentStyle, baseDir);

      if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
        lineWidth = (lineWidth + 1) / 2;
      }

      // Do not decorate the synthetic em-space used for paragraph indentation.
      if (wordTextLen(i) >= 3 && static_cast<uint8_t>(word[0]) == 0xE2 && static_cast<uint8_t>(word[1]) == 0x80 &&
          static_cast<uint8_t>(word[2]) == 0x83) {
        const char* visibleText = word + 3;
        lineStartX += renderer.getTextAdvanceX(wordFont, "\xe2\x80\x83", currentStyle);
        lineWidth = renderer.getTextWidth(wordFont, visibleText, currentStyle, baseDir);
        if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
          lineWidth = (lineWidth + 1) / 2;
        }
      }
      if (letterSpacing) {
        // Word Expansion widened the word; the underline follows. The em-space indent is one
        // glyph, so counting the whole word adds exactly the steps under the visible text.
        lineWidth += letterSpacing * std::max(0, renderer.spacedGlyphCount(wordFont, word, currentStyle) - 2 +
                                                     (lineStartX == drawX ? 1 : 0));
      }

      for (auto& line : decorationLines) {
        if ((currentStyle & line.style) == 0) {
          flushDecoration(line);
          continue;
        }

        int offset = 2;
        if (line.style == EpdFontFamily::STRIKETHROUGH) {
          int32_t advance = 0;
          int height = 0;
          const bool smallCaps = (currentStyle & EpdFontFamily::SMALL_CAPS) != 0;
          if (!renderer.getCodepointMetrics(wordFont, smallCaps ? 'X' : 'x', currentStyle, advance, height) ||
              height <= 0) {
            height = wordAscender * 2 / 3;
          }
          if ((currentStyle & (EpdFontFamily::SUP | EpdFontFamily::SUB)) != 0) {
            height = (height + 1) / 2;
          } else if (smallCaps) {
            height = (height * 3 + 3) / 4;
          }
          offset = -std::max(1, height / 2);
        }
        const int lineY = wordY + wordAscender + offset;
        if (line.active() && line.yPos != lineY) {
          flushDecoration(line);
        }
        if (!line.active()) {
          line.startX = lineStartX;
          line.yPos = lineY;
        }
        line.endX = lineStartX + lineWidth;
      }
    } else {
      flushDecorations();
    }
  }
  flushDecorations();
}
