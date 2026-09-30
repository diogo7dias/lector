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

// Source regression: a screen change drops the old hit rows under the render lock, so a
// tap on the next screen cannot land on a row the last one drew.
TEST(RowHit, ScreenChangeInvalidatesUnderTheRenderLock) {
  const auto read = [](const char* path) {
    std::ifstream file(std::string(REPO_SOURCE_DIR) + path);
    EXPECT_TRUE(file.is_open());
    std::stringstream source;
    source << file.rdbuf();
    return source.str();
  };
  const auto manager = read("/activities/ActivityManager.cpp");
  const auto transition = manager.find("while (pendingAction != PendingAction::None)");
  const auto dispatch = manager.find("if (pendingAction == PendingAction::Pop)", transition);
  ASSERT_NE(transition, std::string::npos);
  ASSERT_NE(dispatch, std::string::npos);
  const auto reset = manager.substr(transition, dispatch - transition);
  EXPECT_NE(reset.find("row_hit::lastRows().begin();"), std::string::npos);
  EXPECT_LT(reset.find("RenderLock lock;"), reset.find("row_hit::lastRows().begin();"));
}
