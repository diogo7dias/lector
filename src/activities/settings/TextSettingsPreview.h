#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class GfxRenderer;
class TextBlock;

namespace textsettings {

// Settings + geometry that determine the laid-out lines; used to invalidate the cache.
// Draw-only settings (vertical margins, the status bar, debug borders, the Paperback Look
// smear) are deliberately NOT here: they change where the cached lines are painted, not
// what the lines are.
struct PreviewKey {
  int fontId = -1;
  int fontPointSize = -1;
  int screenMargin = -1;
  int textWidth = -1;
  float lineCompression = -1.0f;
  uint8_t alignment = 0xFF;
  bool extraParagraphSpacing = false;
  bool focusReading = false;
  bool hyphenation = false;
  bool embeddedStyle = false;
  uint8_t paragraphSpacing = 0xFF;
  uint8_t guideDotsMode = 0xFF;
  uint8_t firstLineIndentMode = 0xFF;
  uint8_t firstLineIndentPercent = 0xFF;
  bool operator==(const PreviewKey&) const = default;
};

// One laid-out line plus the gap that precedes it (paragraph gap, heading gap), so the
// two strips can be drawn from either end of the same list without re-running the parser.
struct PreviewLine {
  std::shared_ptr<TextBlock> line;
  int gapBefore = 0;
};

// Cached engine preview lines + the key that produced them
struct PreviewLayout {
  std::vector<PreviewLine> lines;
  PreviewKey key;
};

// Draws the sample page through the reader engine.
//
// The pane is a SPLIT SLICE of a real page: the top of the page (its top margin, whatever
// the status bar puts up there, the first lines) then a dashed cut, then the bottom of the
// page (last lines, bottom margin, bottom status bar). Both vertical margins are therefore
// on screen at their true pixel size, which a single continuous slice could never show.
//
// The pane spans the full screen width with no padding of its own, so the horizontal
// margin is drawn at exactly the value the page will use.
void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int top, int height);

}  // namespace textsettings
