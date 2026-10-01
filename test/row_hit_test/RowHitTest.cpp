#include "components/RowHitTest.h"

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>
#include <string>

namespace {

row_hit::Rows threeRows() {
  row_hit::Rows rows;
  rows.begin();
  rows.add(7, 0, 100, 480, 56);  // item 7 at the top of the list
  rows.add(8, 0, 156, 480, 56);
  rows.add(9, 0, 212, 480, 84);  // a wrapped row, taller than the others
  return rows;
}

TEST(RowHit, ATapInsideARowNamesTheItemThatRowDrew) {
  const row_hit::Rows rows = threeRows();
  EXPECT_EQ(rows.itemAt(240, 120), 7);
  EXPECT_EQ(rows.itemAt(240, 180), 8);
  EXPECT_EQ(rows.itemAt(240, 280), 9);
}

TEST(RowHit, ATapAboveTheFirstRowNamesNothing) { EXPECT_EQ(threeRows().itemAt(240, 99), row_hit::kNoItem); }

TEST(RowHit, ATapBelowTheLastRowNamesNothing) { EXPECT_EQ(threeRows().itemAt(240, 296), row_hit::kNoItem); }

TEST(RowHit, ATapOutsideTheRowWidthNamesNothing) { EXPECT_EQ(threeRows().itemAt(600, 120), row_hit::kNoItem); }

TEST(RowHit, BeginDropsThePreviousFramesRows) {
  row_hit::Rows rows = threeRows();
  rows.begin();
  EXPECT_EQ(rows.itemAt(240, 120), row_hit::kNoItem);
}

TEST(RowHit, RowsBeyondTheStoreAreDroppedRatherThanOverwritingOthers) {
  row_hit::Rows rows;
  rows.begin();
  for (int i = 0; i < row_hit::kMaxRows + 4; ++i) rows.add(i, 0, i * 20, 480, 20);
  EXPECT_EQ(rows.itemAt(240, 10), 0);
  EXPECT_EQ(rows.itemAt(240, (row_hit::kMaxRows - 1) * 20 + 10), row_hit::kMaxRows - 1);
  EXPECT_EQ(rows.itemAt(240, row_hit::kMaxRows * 20 + 10), row_hit::kNoItem);
}

}  // namespace

// Source regression: the firmware owns the listing and task locks; the host checks
// that invalidation happens before changing row identity, including failed loads.
TEST(RowHit, BrowserInvalidatesBeforeReloadSearchAndNavigation) {
  const auto read = [](const char* path) {
    std::ifstream file(std::string(REPO_SOURCE_DIR) + path);
    EXPECT_TRUE(file.is_open());
    std::stringstream source;
    source << file.rdbuf();
    return source.str();
  };
  const auto browser = read("/activities/home/FileBrowserActivity.cpp");
  const auto loadStart = browser.find("void FileBrowserActivity::loadFiles()");
  const auto scan = browser.find("auto root = Storage.open", loadStart);
  ASSERT_NE(loadStart, std::string::npos);
  ASSERT_NE(scan, std::string::npos);
  const auto load = browser.substr(loadStart, scan - loadStart);
  EXPECT_LT(load.find("row_hit::lastRows().begin();"), load.find("files.clear();"));
  EXPECT_NE(load.find("searchQuery.clear();"), std::string::npos);
  EXPECT_NE(load.find("filtered.clear();"), std::string::npos);
  EXPECT_NE(load.find("folderHasEntries = false;"), std::string::npos);

  const auto searchStart = browser.find("void FileBrowserActivity::applySearch(");
  const auto searchEnd = browser.find("void FileBrowserActivity::clearSearch()", searchStart);
  ASSERT_NE(searchStart, std::string::npos);
  ASSERT_NE(searchEnd, std::string::npos);
  const auto search = browser.substr(searchStart, searchEnd - searchStart);
  EXPECT_LT(search.find("RenderLock lock(*this);"), search.find("row_hit::lastRows().begin();"));
  EXPECT_LT(search.find("row_hit::lastRows().begin();"), search.find("searchQuery = query;"));

  const auto manager = read("/activities/ActivityManager.cpp");
  const auto transition = manager.find("while (pendingAction != PendingAction::None)");
  const auto dispatch = manager.find("if (pendingAction == PendingAction::Pop)", transition);
  ASSERT_NE(transition, std::string::npos);
  ASSERT_NE(dispatch, std::string::npos);
  const auto reset = manager.substr(transition, dispatch - transition);
  EXPECT_NE(reset.find("row_hit::lastRows().begin();"), std::string::npos);
  EXPECT_LT(reset.find("RenderLock lock;"), reset.find("row_hit::lastRows().begin();"));
}
