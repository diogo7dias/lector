// Source audit: a screen on UiGridActivity names its cells and lets the base
// draw them, the chrome around them, and the band an armed number takes over.
// settings_grid::cellAt is banned here too: a screen walking the cells itself is
// a screen with a second copy of the hit test, which is exactly what the base
// was written to delete. The moment one of them paints its own header, hints or rows,
// its type and its spacing stop coming from the theme, and a look change
// silently skips it. That is how these screens drifted apart in the first place.
//
// GUI.drawPopup is allowed: a popup is a shared component the base renders
// through drawOverlay(), not per-screen painting.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef GRID_SCREEN_SOURCES
#error "GRID_SCREEN_SOURCES must be defined by the build system"
#endif

namespace {

struct Line {
  std::string path;
  int number = 0;
  std::string text;
};

std::vector<std::string> sourcePaths() {
  std::vector<std::string> paths;
  std::stringstream all(GRID_SCREEN_SOURCES);
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
    while (std::getline(file, text)) lines.push_back(Line{path, ++number, text});
  }
  return lines;
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

bool isComment(const std::string& text) {
  const std::size_t first = text.find_first_not_of(" \t");
  return first != std::string::npos && text.compare(first, 2, "//") == 0;
}

}  // namespace

TEST(GridScreenAudit, NoGridScreenDrawsForItself) {
  for (const Line& line : sourceLines()) {
    if (isComment(line.text)) continue;
    for (const char* call : {"renderer.drawText", "renderer.drawCenteredText", "renderer.fillRect",
                             "renderer.drawRect", "GUI.drawHeader", "GUI.drawButtonHints", "GUI.drawList",
                             "GUI.drawWrappedList", "GUI.drawSelection", "renderer.displayBuffer",
                             "settings_grid::cellAt"}) {
      EXPECT_FALSE(contains(line.text, call))
          << line.path << ":" << line.number << " calls " << call
          << ". A UiGridActivity screen names its cells and lets the base draw them.";
    }
  }
}

TEST(GridScreenAudit, EveryGridScreenNamesItsCells) {
  for (const std::string& path : sourcePaths()) {
    std::ifstream file(path);
    ASSERT_TRUE(file.is_open()) << "cannot open " << path;
    std::stringstream body;
    body << file.rdbuf();
    EXPECT_NE(body.str().find("cellName"), std::string::npos)
        << path << " is listed as a grid screen but never names a cell.";
  }
}
