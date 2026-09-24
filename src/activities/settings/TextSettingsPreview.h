#pragma once

#include <Epub/ReaderRenderSpec.h>

#include <cstdint>
#include <memory>
#include <vector>

class GfxRenderer;
struct ReaderPrefs;
class TextBlock;

namespace textsettings {

// Settings + geometry that determine the laid-out lines; used to invalidate the cache.
// The render spec is the reader's own (makeRenderSpec), so a field the reader lays out
// with cannot be missed here. Its viewportWidth carries the text column width and its
// height is unused. Draw-only settings (vertical margins, the status bar, debug borders,
// the Paperback Look smear) are deliberately NOT here: they change where the cached lines
// are painted, not what the lines are.
struct PreviewKey {
  ReaderRenderSpec spec;
  int fontPointSize = -1;
  int screenMargin = -1;
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
  // Where the second sample paragraph starts. The pane draws the first paragraph with the
  // Paperback Look smear and the second without it, so the choice is judged on two
  // passages side by side rather than by toggling the setting and remembering.
  int secondParagraphLine = 0;
};

// Draws the sample page through the reader engine.
//
// The pane is the TOP of a real page: its status bar, its top margin, then the sample
// text running down from it. It was a split slice with a dashed cut through it, which put
// both vertical margins on screen at true size; the cut and its labels cost more of the
// pane than the bottom margin was worth, so the bottom margin is now judged from the
// number rather than from the picture.
//
// The pane spans the full screen width with no padding of its own, so the horizontal
// margin is drawn at exactly the value the page will use.
void renderPreview(const GfxRenderer& renderer, PreviewLayout& layout, const ReaderPrefs& look, int top, int height);

}  // namespace textsettings
