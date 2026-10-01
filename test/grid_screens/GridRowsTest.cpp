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

template <class Screen>
void tap(Screen& grid, MappedInputManager& input, int index) {
  const auto rect = grid.uiTarget.fills[index].rect;
  input.snapshot = {};
  input.snapshot.touchX = rect.x + rect.width / 2;
  input.snapshot.touchY = rect.y + rect.height / 2;
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

void expectOutlined(const Grid& grid, int index) {
  const auto& target = grid.uiTarget;
  ASSERT_EQ(target.fills.size(), 3u);
  EXPECT_EQ(target.fills[index].paint.kind, fui::PaintKind::Solid);
  EXPECT_EQ(target.fills[index].paint.color, fui::Color::White) << "armed row must never carry the selected fill";
  ASSERT_EQ(target.strokes.size(), 1u);
  EXPECT_EQ(target.strokes[0].rect.y, target.fills[index].rect.y);
  EXPECT_EQ(target.strokes[0].width, 1);
  EXPECT_EQ(target.strokes[0].paint.color, fui::Color::Black);
  ASSERT_EQ(target.texts.size(), 6u);
  EXPECT_FALSE(target.texts[index * 2].inverted);
  EXPECT_FALSE(target.texts[index * 2 + 1].inverted);
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
  tap(list, input, 0);
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
  tap(list, input, 0);
  EXPECT_EQ(list.activations, 0) << "the swipe cleared the earlier arm";
  tap(list, input, 0);
  EXPECT_EQ(list.activations, 1);
  EXPECT_EQ(list.activated, 0);
}

TEST(GridRows, RoutedSwipeMovesOneRowAndClearsArmWhileTapsStillActivate) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  tap(grid, input, 0);
  ASSERT_TRUE(grid.twoTap().armed());
  swipe(grid, input, MappedInputManager::SwipeDir::Up);
  EXPECT_EQ(grid.selected(), 1);
  EXPECT_EQ(grid.activations, 0);
  EXPECT_FALSE(grid.twoTap().armed());
  swipe(grid, input, MappedInputManager::SwipeDir::Down);
  EXPECT_EQ(grid.selected(), 0);
  tap(grid, input, 0);
  EXPECT_EQ(grid.activations, 0);
  tap(grid, input, 0);
  EXPECT_EQ(grid.activations, 1);
}

TEST(GridRows, ArmingDoesNotSelectOrActivateAndSurvivesRepaints) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  ASSERT_EQ(grid.selected(), 0);
  tap(grid, input, 1);
  EXPECT_EQ(grid.selected(), 0) << "an arming tap must not change the grid selection";
  EXPECT_EQ(grid.activations, 0);
  EXPECT_EQ(grid.app.refreshHint(), fui::RefreshHint::Fast);
  for (int repaint = 0; repaint < 3; ++repaint) {
    grid.paint();
    EXPECT_TRUE(grid.twoTap().armed());
    EXPECT_TRUE(grid.app.touchActive());
    expectOutlined(grid, 1);
  }
  tap(grid, input, 1);
  EXPECT_EQ(grid.selected(), 1);
  EXPECT_EQ(grid.activations, 1);
  EXPECT_EQ(grid.activated, 1);
  EXPECT_FALSE(grid.twoTap().armed());
}

TEST(GridRows, ArmingTheSelectedRowOverridesItsFillAndText) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  ASSERT_EQ(grid.uiTarget.fills[0].paint.kind, fui::PaintKind::Solid);
  ASSERT_EQ(grid.uiTarget.fills[0].paint.color, fui::Color::Black);
  ASSERT_TRUE(grid.uiTarget.texts[0].inverted);
  tap(grid, input, 0);
  grid.paint();
  expectOutlined(grid, 0);
  grid.clearTwoTap();
  grid.paint();
  EXPECT_FALSE(grid.app.touchActive());
  EXPECT_EQ(grid.uiTarget.fills[0].paint.color, fui::Color::Black);
  EXPECT_TRUE(grid.uiTarget.texts[0].inverted);
}

TEST(GridRows, KeysOnlySelectionKeepsItsFilledBandAndGateStaysOff) {
  GfxRenderer renderer;
  MappedInputManager input;
  input.touch = false;
  Grid grid(renderer, input);
  grid.onEnter();
  grid.paint();
  grid.loop();
  EXPECT_FALSE(grid.twoTap().enabled());
  EXPECT_FALSE(grid.app.touchActive());
  EXPECT_EQ(grid.uiTarget.fills[0].paint.kind, fui::PaintKind::Solid);
  EXPECT_EQ(grid.uiTarget.fills[0].paint.color, fui::Color::Black);
  EXPECT_TRUE(grid.uiTarget.texts[0].inverted);
  EXPECT_TRUE(grid.uiTarget.strokes.empty());
}

TEST(GridRows, OtherTouchBoardsKeepTheirGridAndArmedTextLegible) {
  GfxRenderer renderer;
  MappedInputManager input;
  Grid grid(renderer, input);
  display.device.isX4Pro = false;
  grid.onEnter();
  grid.paint();
  EXPECT_LT(grid.uiTarget.fills[0].rect.width, renderer.getScreenWidth());
  tap(grid, input, 0);
  grid.paint();
  EXPECT_EQ(grid.uiTarget.fills[0].paint.color, fui::Color::White);
  EXPECT_FALSE(grid.uiTarget.texts[0].inverted);
  EXPECT_FALSE(grid.uiTarget.texts[1].inverted);
  EXPECT_EQ(grid.activations, 0);
  tap(grid, input, 0);
  EXPECT_EQ(grid.activations, 1);
  display.device.isX4Pro = true;
}
