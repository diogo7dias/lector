#include <components/lists/list.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <vector>

using namespace freeink::ui;

namespace {
ListItem header(const char* label) {
  ListItem item;
  item.label = label;
  item.isHeader = true;
  return item;
}

const std::array<ListItem, 6> menu = {header("First"),  ListItem{"One"},   ListItem{"Two"},
                                      header("Second"), ListItem{"Three"}, header("Empty")};

std::vector<int> visible(const ListSections& sections, const ListItem* items = menu.data(),
                         const int count = menu.size()) {
  std::vector<int> indexes;
  for (int i = 0; i < sections.visibleCount(items, count); ++i) indexes.push_back(sections.itemIndex(items, count, i));
  return indexes;
}

class RecordingTarget : public DrawTarget {
 public:
  struct Fill {
    Rect rect;
    Color color;
  };
  std::vector<Fill> fills;
  std::vector<const char*> labels;
  Size measureText(FontId, const char* text, TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 6), 12};
  }
  int16_t lineHeight(FontId) const override { return 12; }
  void fill(Rect rect, Paint paint, uint8_t, uint8_t) override { fills.push_back({rect, paint.color}); }
  void stroke(Rect, Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect, const char* label, TextStyle) override { labels.push_back(label); }
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};

struct RenderMenu {
  RecordingTarget target;
  DeviceContext device;
  InputSnapshot input;
  InteractionBuffer<16> hits;
  ListProps props;
  ListNav nav;
  ListSections sections;
  RenderMenu() {
    device.width = 240;
    device.height = 400;
    device.hasTouch = true;
    props.items = menu.data();
    props.count = menu.size();
    props.sections = &sections;
    props.action = 42;
    props.rowHeight = 44;
    props.headerText.color = Color::White;
    props.headerFillHugsText = true;
    props.headerUnderline = false;
  }
  void draw() {
    target.fills.clear();
    target.labels.clear();
    nav.syncToProps(device.screen(), 40, 0, sections.visibleCount(menu.data(), menu.size()), props);
    Frame<16> frame(target, device, input, hits);
    list(frame, device.screen(), props);
  }
};
}  // namespace

TEST(CollapsibleSections, EntryIsAllClosedWithCursorOnFirstHeader) {
  RenderMenu view;
  view.draw();
  static_assert(sizeof(ListSections) == sizeof(int));
  EXPECT_EQ(view.sections.expandedHeader, -1);
  EXPECT_EQ(view.nav.selected, 0);
  EXPECT_EQ(visible(view.sections), (std::vector<int>{0, 3, 5}));
  ASSERT_EQ(view.target.labels.size(), 3u);
  EXPECT_STREQ(view.target.labels[0], "First");
}

TEST(CollapsibleSections, FocusBetweenHeadersOpensOnlyTheDestination) {
  ListSections sections;
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 0), 0);
  EXPECT_EQ(visible(sections), (std::vector<int>{0, 1, 2, 3, 5}));
  // Next header was visible row 3, and moves to row 1 when the first closes.
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 3), 1);
  EXPECT_EQ(sections.expandedHeader, 3);
  EXPECT_EQ(visible(sections), (std::vector<int>{0, 3, 4, 5}));
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 0), 0);
  EXPECT_EQ(sections.expandedHeader, 0);
  EXPECT_EQ(visible(sections), (std::vector<int>{0, 1, 2, 3, 5}));
}

TEST(CollapsibleSections, WalkDownIntoAndOutOfSection) {
  ListSections sections;
  sections.focus(menu.data(), menu.size(), 0);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 1), 1);
  EXPECT_EQ(sections.expandedHeader, 0);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 2), 2);
  EXPECT_EQ(sections.expandedHeader, 0);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 3), 1);
  EXPECT_EQ(sections.expandedHeader, 3);
  EXPECT_EQ(sections.itemIndex(menu.data(), menu.size(), 2), 4);
}

TEST(CollapsibleSections, WalkUpThroughOwnHeaderThenPreviousHeader) {
  ListSections sections;
  sections.focus(menu.data(), menu.size(), 1);                // Second
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 2), 2);  // Three
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 1), 1);  // Second
  EXPECT_EQ(sections.expandedHeader, 3);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 0), 0);  // First
  EXPECT_EQ(sections.expandedHeader, 0);
}

TEST(CollapsibleSections, BackCollapsesToHeaderThenSignalsExit) {
  ListSections sections;
  sections.focus(menu.data(), menu.size(), 1);
  sections.focus(menu.data(), menu.size(), 2);
  EXPECT_EQ(sections.collapse(menu.data(), menu.size()), 1);
  EXPECT_EQ(sections.expandedHeader, -1);
  EXPECT_EQ(visible(sections), (std::vector<int>{0, 3, 5}));
  EXPECT_EQ(sections.collapse(menu.data(), menu.size()), -1);
}

TEST(CollapsibleSections, EmptyHeadingOpensAndBackClosesIt) {
  ListSections sections;
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 2), 2);
  EXPECT_EQ(sections.expandedHeader, 5);
  EXPECT_EQ(visible(sections), (std::vector<int>{0, 3, 5}));
  EXPECT_EQ(sections.collapse(menu.data(), menu.size()), 2);
}

TEST(CollapsibleSections, ConsecutiveHeadingsRemainLandable) {
  const ListItem items[] = {header("A"), header("B"), {"Child"}};
  ListSections sections;
  EXPECT_EQ(sections.focus(items, 3, 0), 0);
  EXPECT_EQ(visible(sections, items, 3), (std::vector<int>{0, 1}));
  EXPECT_EQ(sections.focus(items, 3, 1), 1);
  EXPECT_EQ(visible(sections, items, 3), (std::vector<int>{0, 1, 2}));
  EXPECT_EQ(sections.focus(items, 3, 0), 0);
  EXPECT_EQ(sections.expandedHeader, 0);
}

TEST(CollapsibleSections, LeadingRowsStayVisibleAndCloseSectionWhenFocused) {
  const ListItem items[] = {{"Loose"}, header("A"), {"Child"}};
  ListSections sections;
  EXPECT_EQ(visible(sections, items, 3), (std::vector<int>{0, 1}));
  EXPECT_EQ(sections.focus(items, 3, 1), 1);
  EXPECT_EQ(visible(sections, items, 3), (std::vector<int>{0, 1, 2}));
  EXPECT_EQ(sections.focus(items, 3, 0), 0);
  EXPECT_EQ(sections.expandedHeader, -1);
  EXPECT_EQ(sections.collapse(items, 3), -1);
}

TEST(CollapsibleSections, EmptyListAndInvalidFocusAreSafe) {
  ListSections sections;
  EXPECT_EQ(sections.visibleCount(nullptr, 0), 0);
  EXPECT_EQ(sections.focus(nullptr, 0, 0), -1);
  EXPECT_EQ(sections.collapse(nullptr, 0), -1);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), -1), -1);
  EXPECT_EQ(sections.focus(menu.data(), menu.size(), 99), -1);
  EXPECT_EQ(sections.expandedHeader, -1);
}

TEST(CollapsibleSections, TapHeaderRoutesVisibleIndexAndOpensSection) {
  RenderMenu view;
  view.draw();
  ASSERT_EQ(view.hits.count(), 3u);
  const Rect rect = view.hits.data()[1].rect;
  InputSnapshot tap;
  tap.touchReleased = true;
  tap.touchX = rect.x + rect.width / 2;
  tap.touchY = rect.y + rect.height / 2;
  const ActionEvent event = view.hits.route(tap);
  ASSERT_EQ(event.action, 42);
  ASSERT_EQ(event.value, 1);
  view.nav.selected = view.sections.focus(menu.data(), menu.size(), event.value);
  view.draw();
  EXPECT_EQ(view.sections.expandedHeader, 3);
  ASSERT_EQ(view.hits.count(), 4u);
  EXPECT_EQ(view.hits.data()[2].value, 2);
  EXPECT_STREQ(view.target.labels[2], "Three");
}

TEST(CollapsibleSections, HeaderFeedbackFinishesFollowWithoutRebuild) {
  RenderMenu view;
  view.nav.selected = 2;
  view.draw();
  EXPECT_FALSE(view.nav.followPending);
  EXPECT_FALSE(view.nav.consumeRebuildNeeded());
  EXPECT_EQ(view.nav.drawnRows, 3);
}

TEST(CollapsibleSections, BandStillHugsLabelAndIndicatorShowsClosedVersusOpen) {
  RenderMenu view;
  view.draw();
  ASSERT_GE(view.target.fills.size(), 3u);
  const auto band = view.target.fills[0];
  EXPECT_EQ(band.color, Color::Black);
  EXPECT_EQ(band.rect.width, 5 * 6 + 12 + 2 * view.props.headerFillPadX);
  EXPECT_LT(band.rect.width, view.device.width);
  // Closed plus has a horizontal and vertical stroke in white.
  EXPECT_EQ(view.target.fills[1].color, Color::White);
  EXPECT_EQ(view.target.fills[1].rect.height, 1);
  EXPECT_EQ(view.target.fills[2].rect.width, 1);
  view.sections.focus(menu.data(), menu.size(), 0);
  view.draw();
  EXPECT_EQ(view.target.fills[1].rect.height, 1);
  // Open minus is followed by the first child row's background, not a vertical stroke.
  EXPECT_GT(view.target.fills[2].rect.width, 1);
}

TEST(CollapsibleSections, DefaultOffKeepsLegacyHeadersNonInteractiveAndUnadorned) {
  RenderMenu view;
  view.props.sections = nullptr;
  view.draw();
  ASSERT_EQ(view.hits.count(), 3u);  // only the three ordinary rows
  EXPECT_EQ(view.target.labels.size(), menu.size());
  EXPECT_EQ(view.target.fills[0].rect.width, 5 * 6 + 2 * view.props.headerFillPadX);
  EXPECT_EQ(view.target.fills[1].color, Color::White);  // child background follows band
  EXPECT_EQ(view.target.fills[1].rect.height, 44);
}

TEST(CollapsibleSections, TouchTargetsDoNotOverlapHeaderOrChild) {
  RenderMenu view;
  view.sections.focus(menu.data(), menu.size(), 0);
  view.draw();
  ASSERT_GT(view.hits.count(), 1u);
  EXPECT_GE(view.hits.data()[0].rect.height, view.device.minTouchSize);
  for (size_t i = 1; i < view.hits.count(); ++i) {
    EXPECT_LE(view.hits.data()[i - 1].rect.bottom(), view.hits.data()[i].rect.y);
  }
}

TEST(CollapsibleSections, RejectsPartialSourceArrayBeforeReadingIt) {
  RenderMenu view;
  const ListItem one = header("Only materialized row");
  view.props.items = &one;
  view.props.itemsWindowCount = 1;
  view.draw();
  EXPECT_EQ(view.hits.count(), 0u);
  EXPECT_TRUE(view.target.labels.empty());
}
