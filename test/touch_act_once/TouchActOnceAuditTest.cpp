// Source audit: a screen that acts on the touch-down itself must spend the contact.
//
// wasScreenTouchDown() is level-triggered: while the finger stays down and inside tap
// slop it answers true on every pass. A screen that acts on the first true and then
// changes what its cells mean acts again on the next pass, on whatever the redraw put
// under the same finger. The settings grids hit exactly that: opening a category and
// toggling one of its settings from a single touch. takeScreenTouchDown() suppresses the
// rest of the contact, which is the only correct query for an acting screen.
//
// Drag tracking is the one legitimate use of the level-triggered form, and it always
// pairs with isScreenTouchHeld() on the same line, so that is the exemption.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef TOUCH_ACT_ONCE_SOURCES
#error "TOUCH_ACT_ONCE_SOURCES must be defined by the build system"
#endif

namespace {

struct Line {
  std::string path;
  int number = 0;
  std::string text;
};

std::vector<std::string> sourcePaths() {
  std::vector<std::string> paths;
  std::stringstream all(TOUCH_ACT_ONCE_SOURCES);
  std::string path;
  while (std::getline(all, path, '|')) {
    if (!path.empty()) paths.push_back(path);
  }
  return paths;
}

std::vector<Line> sourceLines() {
  std::vector<Line> lines;
  for (const std::string& path : sourcePaths()) {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path;
    std::string text;
    int number = 0;
    while (std::getline(file, text)) {
      lines.push_back(Line{path, ++number, text});
    }
  }
  return lines;
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

bool isComment(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t");
  return first != std::string::npos && text.compare(first, 2, "//") == 0;
}

}  // namespace

TEST(TouchActOnceAudit, ActingScreensSpendTheContact) {
  for (const Line& line : sourceLines()) {
    if (isComment(line.text)) continue;
    if (!contains(line.text, "wasScreenTouchDown")) continue;
    EXPECT_TRUE(contains(line.text, "isScreenTouchHeld"))
        << line.path << ":" << line.number << " reads the level-triggered touch-down without dragging. "
        << "Use takeScreenTouchDown() so the contact cannot act twice.";
  }
}

TEST(TouchActOnceAudit, TheGridsLetRoutingPickForThem) {
  // Both grids moved onto UiGridActivity, where a tap is dispatched once by the
  // interaction table the paint published. A grid asking the input manager for a
  // touch itself is a grid that has grown a second, unspent hit test again.
  for (const Line& line : sourceLines()) {
    if (isComment(line.text)) continue;
    for (const char* query : {"wasScreenTouchDown", "takeScreenTouchDown", "isScreenTouchHeld"}) {
      EXPECT_FALSE(contains(line.text, query))
          << line.path << ":" << line.number << " asks for " << query
          << " itself. UiGridActivity routes the touch and dispatches the cell once.";
    }
  }
}
