// Source audit: every surface that highlights a focused row must go through
// BaseTheme::drawSelection, so one setting reaches all of them. A new list that
// fills its own selection rectangle would silently ignore the setting, and the
// bug would only show on the styles nobody tests by hand.
#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef SELECTION_SOURCES
#error "SELECTION_SOURCES must be defined by the build system"
#endif

namespace {

// Every file that paints a focused row. Hand-rolled lists outside BaseTheme are
// included so a surface cannot opt out of the setting by drawing its own list.
std::vector<std::string> sourcePaths() {
  std::vector<std::string> paths;
  std::stringstream all(SELECTION_SOURCES);
  std::string path;
  while (std::getline(all, path, '|')) {
    if (!path.empty()) paths.push_back(path);
  }
  return paths;
}

std::vector<std::string> sourceLines() {
  std::vector<std::string> lines;
  for (const std::string& path : sourcePaths()) {
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << "cannot open " << path;
    std::string line;
    while (std::getline(file, line)) lines.push_back(line);
  }
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
    if (!contains(line, "fillRect") && !contains(line, "fillRoundedRect")) continue;
    // Any flag a surface could use to mean "this row has focus". `inverted` and
    // `cardInverted` are the names the shared painter's callers use today; the older
    // `selected` / `bookSelected` spellings are kept so a revert cannot slip through.
    if (!contains(line, "elected") && !contains(line, "nverted")) continue;
    // The popup's own row background is drawn for every row, focused or not, and its
    // colour is already decided by paintedOver.
    if (contains(line, "rowColor")) continue;
    // Badge chips and label boxes are allowed to follow the selection's colour; what
    // no surface may do is decide the selection's SHAPE, which is what a fill spanning
    // the whole row is. Match on the row-sized extents the lists measure.
    static const char* const kRowExtents[] = {"rect.width", "rowW",         "row.height", "entry.height", "itemRectW",
                                              "bookWidth",  "screen.width", "pageWidth",  "contentWidth"};
    bool rowSized = false;
    for (const char* extent : kRowExtents) {
      if (contains(line, extent)) rowSized = true;
    }
    if (!rowSized) continue;
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
  // bookmarks list, and the Continue Reading card, which marks itself in exactly two
  // places: the no-cover card fill and the single title-box site shared by a freshly
  // rendered cover and a restored one, plus the one hand-rolled list left outside
  // BaseTheme: the XTC chapter list. The nearby peer list and the OPDS browser were
  // the others until they moved onto UiStatusActivity, where FreeInkUI marks the
  // selection.
  EXPECT_GE(calls, 9);
}
