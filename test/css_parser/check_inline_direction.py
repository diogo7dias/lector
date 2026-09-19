"""Run the production style resolver without the parser's hardware dependencies."""
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[2]
parser = root / "lib/Epub/Epub/parsers/ChapterHtmlSlimParser"
header = parser.with_suffix(".h").read_text()
source = parser.with_suffix(".cpp").read_text()
# Use the actual fields and method, not a second implementation of inheritance.
fields = header[header.index("  struct StyleStackEntry {"):header.index("  static constexpr size_t MAX_GRID_TABLE_COLUMNS")]
method = source[source.index("void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {"):source.index("void ChapterHtmlSlimParser::flushPendingAnchor() {")]
program = r'''
#include <cassert>
#include <vector>
#include "Epub/blocks/BlockStyle.h"
struct TextBlockStub {
  BlockStyle style;
  bool empty = true;
  bool isEmpty() const { return empty; }
  BlockStyle& getBlockStyle() { return style; }
};
struct ChapterHtmlSlimParser {
''' + fields + r'''
  TextBlockStub* currentTextBlock = nullptr;
  void updateEffectiveInlineStyle();
};
''' + method + r'''
int main() {
  ChapterHtmlSlimParser p;
  TextBlockStub text;
  p.currentTextBlock = &text;
  BlockStyle parent;
  parent.directionDefined = true;
  parent.isRtl = true;
  p.blockStyleStack.push_back(parent);
  ChapterHtmlSlimParser::StyleStackEntry inlineLtr;
  inlineLtr.hasDirection = true;
  inlineLtr.direction = CssTextDirection::Ltr;
  p.inlineStyleStack.push_back(inlineLtr);
  p.updateEffectiveInlineStyle();
  assert(text.style.directionDefined && text.style.isRtl);
  assert(p.effectiveDirectionDefined && p.effectiveDirection == CssTextDirection::Ltr);
  // A table/body entry establishes flow direction, unlike an inline span.
  p.inlineStyleStack.back().setsParagraphDirection = true;
  p.updateEffectiveInlineStyle();
  assert(text.style.directionDefined && !text.style.isRtl);
  p.inlineStyleStack.clear();
  p.updateEffectiveInlineStyle();
  assert(text.style.isRtl);
  // Already emitted text must not be restyled by a later direction change.
  text.empty = false;
  p.blockStyleStack.back().isRtl = false;
  p.updateEffectiveInlineStyle();
  assert(text.style.isRtl);
  text.empty = true;
  p.blockStyleStack.clear();
  p.inlineStyleStack.push_back(inlineLtr);
  p.updateEffectiveInlineStyle();
  assert(!text.style.directionDefined && !text.style.isRtl);
}
'''
with tempfile.TemporaryDirectory() as directory:
    cpp = Path(directory) / "direction.cpp"
    exe = Path(directory) / "direction"
    cpp.write_text(program)
    subprocess.run([sys.argv[1] if len(sys.argv) > 1 else "c++", "-std=c++20",
                    "-fno-exceptions", "-fno-rtti", "-I", str(root / "lib/Epub"),
                    str(cpp), "-o", str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
print("Inline direction regression passed")
