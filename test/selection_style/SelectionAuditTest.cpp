// Source audit: every surface that highlights a focused row must go through
// BaseTheme::drawSelection, so one setting reaches all of them. A new list that
// fills its own selection rectangle would silently ignore the setting, and the
// bug would only show on the styles nobody tests by hand.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef BASE_THEME_SOURCE
#error "BASE_THEME_SOURCE must be defined by the build system"
#endif

namespace {

std::vector<std::string> sourceLines() {
  std::ifstream file(BASE_THEME_SOURCE);
  EXPECT_TRUE(file.is_open()) << "cannot open " << BASE_THEME_SOURCE;
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(file, line)) lines.push_back(line);
  return lines;
}

bool contains(const std::string& haystack, const char* needle) { return haystack.find(needle) != std::string::npos; }

}  // namespace

TEST(SelectionAudit, NoSurfaceFillsItsOwnSelectionRectangle) {
  const auto lines = sourceLines();
  ASSERT_FALSE(lines.empty());

  std::vector<std::string> offenders;
  for (size_t i = 0; i < lines.size(); ++i) {
    const std::string& line = lines[i];
    if (!contains(line, "fillRect")) continue;
    if (!contains(line, "elected")) continue;  // matches `selected` and `bookSelected`
    std::ostringstream out;
    out << "line " << (i + 1) << ": " << line;
    offenders.push_back(out.str());
  }

  EXPECT_TRUE(offenders.empty()) << "these fill a selection directly instead of calling drawSelection():\n"
                                 << [&offenders] {
                                      std::ostringstream joined;
                                      for (const auto& o : offenders) joined << "  " << o << "\n";
                                      return joined.str();
                                    }();
}

TEST(SelectionAudit, EverySelectionSurfaceCallsTheSharedPainter) {
  const auto lines = sourceLines();
  int calls = 0;
  for (const std::string& line : lines) {
    if (contains(line, "BaseTheme::")) continue;  // the definition itself
    if (contains(line, "drawSelection(")) calls++;
  }
  // drawList, drawWrappedList, drawButtonMenu, drawOptionPopup, the tab bar, the
  // bookmarks list, and the three branches of the Continue Reading card.
  EXPECT_GE(calls, 9);
}
