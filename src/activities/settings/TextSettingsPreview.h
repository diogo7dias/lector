#pragma once

#include <cstdint>
#include <memory>
#include <vector>

class GfxRenderer;
class TextBlock;

namespace textsettings {

// Settings + geometry that determine the laid-out lines; used to invalidate the cache.
// Draw-only settings (paragraph spacing gap, vertical margins, debug borders, the
// Paperback Look smear) are deliberately NOT here: they change where the cached lines
// are painted, not what the lines are.
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
  uint8_t guideDotsMode = 0xFF;
  uint8_t firstLineIndentMode = 0xFF;
  uint8_t firstLineIndentPercent = 0xFF;
  bool operator==(const PreviewKey&) const = default;
};

// Cached engine preview lines + the key that produced them
struct PreviewLayout {
  std::vector<std::shared_ptr<TextBlock>> lines;
  PreviewKey key;
};

// Draws the sample-text pane via the reader engine, reusing layout across redraws
void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, int previewPadding, int labelGap, int top,
                   int height, const char* familyName, const char* sizeName);

}  // namespace textsettings
