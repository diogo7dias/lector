#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace BidiUtils {

// Paragraph base direction for the Unicode BiDi algorithm (UAX#9).
// AUTO: scan text for first strong directional character (P2/P3 rules)
// LTR:  force left-to-right paragraph embedding level
// RTL:  force right-to-left paragraph embedding level
enum class BidiBaseDir : signed char { AUTO = -1, LTR = 0, RTL = 1 };

// Paragraph-level P2/P3: scan the first N strong chars per word to find base direction.
inline constexpr int RTL_PARAGRAPH_PROBE_DEPTH = 5;

bool startsWithRtl(const char* utf8, int maxStrongChars = RTL_PARAGRAPH_PROBE_DEPTH);

// First strong character decides (P2/P3); `fallback` (LTR or RTL) when none is found.
BidiBaseDir detectParagraphLevel(const char* utf8, BidiBaseDir fallback = BidiBaseDir::LTR, int maxStrongChars = 64);

// True for RTL-script non-spacing marks (Hebrew niqqud/cantillation, Arabic
// harakat and Quranic annotation): zero-width for measurement, transparent for
// Arabic joining, and rendered as overlays on the preceding base glyph when
// the active font carries their glyphs (SD fonts; built-in fonts don't).
// Latin combining marks (U+0300-U+036F) intentionally return false — they are
// handled by the utf8IsCombiningMark() rendering path.
bool isTransparentMark(uint32_t cp);

bool applyBidiVisual(const char* utf8, std::string& out, BidiBaseDir baseDir = BidiBaseDir::AUTO);

bool computeVisualWordOrder(const std::vector<std::string>& words, bool paragraphIsRtl,
                            std::vector<uint16_t>& visualOrder);

}  // namespace BidiUtils
