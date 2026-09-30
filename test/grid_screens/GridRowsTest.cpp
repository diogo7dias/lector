#include <gtest/gtest.h>

#include "GridHostAdapters.h"

namespace {
class Grid : public UiGridActivity {
 public:
  Grid(GfxRenderer& renderer, MappedInputManager& input) : UiGridActivity("test", renderer, input) {
    ButtonNavigator::setMappedInputManager(input);
  }
  int activations = 0;
  int activated = -1;
  using UiAppHost::app;
  using UiAppHost::clearTwoTap;
  using UiAppHost::routeTouch;
  using UiAppHost::twoTap;
  using UiAppHost::uiTarget;
  using UiGridActivity::selected;
  Rect rowBand() const { return gridPane(); }
  int cellCount() const override { return 3; }
  const char* cellName(int) const override { return "Setting"; }
  const char* cellValue(int) const override { return "Value"; }
  void activateCell(int index) override {
    ++activations;
    activated = index;
  }
  void paint() {
    uiTarget.fills.clear();
    uiTarget.strokes.clear();
    uiTarget.texts.clear();
    render(RenderLock(*this));
  }
};

class List : public UiListActivity {
 public:
  List(GfxRenderer& renderer, MappedInputManager& input) : UiListActivity("test", renderer, input) {
    ButtonNavigator::setMappedInputManager(input);
  }
  int activations = 0;
  int activated = -1;
  using UiAppHost::twoTap;
  using UiAppHost::uiTarget;
  using UiListActivity::nav;
  Rect rowBand() const { return listBand; }
  int listCount() const override { return 100; }
  void activateIndex(int index) override {
    ++activations;
    activated = index;
  }
  void buildScreen(UiScreen& screen) override {
    fui::ListItem rows[100]{};
    for (int i = 0; i < 100; ++i) {
      rows[i].label = "Chapter";
      rows[i].actionValue = i;
    }
    fui::ListProps props{};
    props.items = rows;
    props.count = 100;
    props.action = ACTION_ROW;
    syncListViewport(screen, props);
    screen.list(props);
  }
  void paint() {
    uiTarget.fills.clear();
    render(RenderLock(*this));
  }
};

// A tap on the middle of one contents row: rows are ROW_H tall from the top of the pane.
template <class Screen>
void tapRow(Screen& grid, MappedInputManager& input, int index) {
  const Rect pane = grid.rowBand();
  input.snapshot = {};
  input.snapshot.touchX = pane.x + pane.width / 2;
  input.snapshot.touchY = pane.y + fui::contents::ROW_H * index + fui::contents::ROW_H / 2;
  input.snapshot.touchPressed = true;
  grid.loop();
  input.snapshot.touchPressed = false;
  input.snapshot.touchReleased = true;
  grid.loop();
  input.snapshot = {};
}

template <class Screen>
void swipe(Screen& screen, MappedInputManager& input, MappedInputManager::SwipeDir direction) {
  input.snapshot = {};
  input.snapshot.touchPressed = true;
  input.snapshot.touchX = 240;
  input.snapshot.touchY = 400;
  screen.loop();
  // touchSnapshotFrom sends non-tap releases off-target. The SDK already
  // classified the travel; UiAppHost still routes this frame with no action.
  input.snapshot = {};
  input.snapshot.touchReleased = true;
  input.snapshot.touchX = input.snapshot.touchY = -1;
  input.swipe = direction;
  screen.loop();
  input.snapshot = {};
  input.swipe = MappedInputManager::SwipeDir::None;
}

}  // namespace

TEST(GridRows, RoutedSwipeReleaseIsNotADispatchedAction) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  input.snapshot.touchReleased = true;
  input.snapshot.touchX = input.snapshot.touchY = -1;
  const auto route = grid.routeTouch(input);
  EXPECT_TRUE(route.routed);
  EXPECT_FALSE(static_cast<bool>(route));
  EXPECT_EQ(grid.activations, 0);
}

TEST(ListRows, RoutedSwipePagesAndClearsArmWhileTapsStillActivate) {
  GfxRenderer renderer;
  MappedInputManager input;
  List list(renderer, input);
  list.onEnter();
  list.paint();
  tapRow(list, input, 0);
  ASSERT_TRUE(list.twoTap().armed());
  EXPECT_EQ(list.activations, 0);
  const int selected = list.nav.selected;
  const int page = list.nav.pageRows();
  ASSERT_GT(page, 1);
  ASSERT_LT(page * 2, list.listCount());
  swipe(list, input, MappedInputManager::SwipeDir::Up);
  EXPECT_EQ(list.nav.top, page);
  EXPECT_EQ(list.nav.selected, selected);
  EXPECT_EQ(list.activations, 0);
  EXPECT_FALSE(list.twoTap().armed());
  list.paint();
  EXPECT_EQ(list.nav.top, page) << "repaint must keep the swiped viewport";
  swipe(list, input, MappedInputManager::SwipeDir::Down);
  EXPECT_EQ(list.nav.top, 0);
  list.paint();
  tapRow(list, input, 0);
  EXPECT_EQ(list.activations, 0) << "the swipe cleared the earlier arm";
  tapRow(list, input, 0);
  EXPECT_EQ(list.activations, 1);
  EXPECT_EQ(list.activated, 0);
}

TEST(GridRows, RoutedSwipeMovesOneRowAndClearsArmWhileTapsStillActivate) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  tapRow(grid, input, 0);
  ASSERT_TRUE(grid.twoTap().armed());
  swipe(grid, input, MappedInputManager::SwipeDir::Up);
  EXPECT_EQ(grid.selected(), 1);
  EXPECT_EQ(grid.activations, 0);
  EXPECT_FALSE(grid.twoTap().armed());
  swipe(grid, input, MappedInputManager::SwipeDir::Down);
  EXPECT_EQ(grid.selected(), 0);
  tapRow(grid, input, 0);
  EXPECT_EQ(grid.activations, 0);
  tapRow(grid, input, 0);
  EXPECT_EQ(grid.activations, 1);
}

TEST(GridRows, ArmingDoesNotSelectOrActivateAndSurvivesRepaints) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  ASSERT_EQ(grid.selected(), 0);
  tapRow(grid, input, 1);
  EXPECT_EQ(grid.selected(), 0) << "an arming tap must not change the grid selection";
  EXPECT_EQ(grid.activations, 0);
  EXPECT_EQ(grid.app.refreshHint(), fui::RefreshHint::Fast);
  for (int repaint = 0; repaint < 3; ++repaint) {
    grid.paint();
    EXPECT_TRUE(grid.twoTap().armed());
    EXPECT_TRUE(grid.app.touchActive());
  }
  tapRow(grid, input, 1);
  EXPECT_EQ(grid.selected(), 1);
  EXPECT_EQ(grid.activations, 1);
  EXPECT_EQ(grid.activated, 1);
  EXPECT_FALSE(grid.twoTap().armed());
}
