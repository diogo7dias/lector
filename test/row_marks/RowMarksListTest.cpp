// The Wi-Fi marks are painted after fui::list() from row_marks::placeRows(). Pin that
// arithmetic against the real list: each row's empty value is drawn at the left edge of
// the slot the list held back (valueInset), on the row's own band.
#include <components/lists/list.h>
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "components/RowMarks.h"

using namespace freeink::ui;

namespace {

class RecordingTarget : public DrawTarget {
 public:
  std::vector<Rect> emptyTexts;  // where the list drew each row's empty value
  Size measureText(FontId, const char* text, TextStyle) const override {
    return {static_cast<int16_t>(std::strlen(text) * 6), 12};
  }
  int16_t lineHeight(FontId) const override { return 12; }
  void fill(Rect, Paint, uint8_t, uint8_t) override {}
  void stroke(Rect, Paint, uint8_t, uint8_t, uint8_t) override {}
  void line(Point, Point, uint8_t, Paint) override {}
  void triangle(Point, Point, Point, Paint) override {}
  void text(Rect rect, const char* label, TextStyle) override {
    if (label != nullptr && label[0] == '\0') emptyTexts.push_back(rect);
  }
  void bitmap(Rect, BitmapRef, BitmapMode, Paint, Rotation) override {}
};

constexpr int kSlot = 70;

void checkAgainstList(const int count, const int topIndex, const int16_t rowInset, const uint8_t scrollSide) {
  std::vector<ListItem> items(static_cast<size_t>(count));
  for (auto& item : items) {
    item.label = "network";
    item.value = "";
  }
  ListProps props;
  props.items = items.data();
  props.count = static_cast<uint16_t>(count);
  props.topIndex = static_cast<uint16_t>(topIndex);
  props.action = 7;
  props.labelText.maxLines = 1;
  props.rowHeight = 40;
  props.rowGap = 4;
  props.rowInset = rowInset;
  props.sidePadding = 10;
  props.scrollIndicatorWidth = 3;
  props.scrollIndicatorInset = 1;
  props.scrollIndicatorSide = scrollSide;
  props.valueInset = kSlot;

  RecordingTarget target;
  DeviceContext device;
  device.width = 480;
  device.height = 800;
  InputSnapshot input;
  InteractionBuffer<32> hits;
  const Rect body{0, 60, 480, 300};
  Frame<32> frame(target, device, input, hits);
  list(frame, body, props);

  row_marks::ListLayout layout;
  layout.band = {body.x, body.y, body.width, body.height};
  layout.count = count;
  layout.visible = listVisibleRows(body, props.rowHeight, props.rowGap);
  layout.topIndex = topIndex;
  layout.rowHeight = props.rowHeight;
  layout.rowGap = props.rowGap;
  layout.rowInset = props.rowInset;
  layout.sidePadding = props.sidePadding;
  layout.scrollWidth = props.scrollIndicatorWidth;
  layout.scrollInset = props.scrollIndicatorInset;
  layout.scrollLeft = scrollSide == 1;
  const row_marks::RowPlacement place = row_marks::placeRows(layout);

  ASSERT_EQ(static_cast<int>(target.emptyTexts.size()), place.end - place.top) << "rows drawn";
  for (int i = place.top; i < place.end; ++i) {
    const Rect& drawn = target.emptyTexts[static_cast<size_t>(i - place.top)];
    EXPECT_EQ(drawn.x, place.slotRight - kSlot) << "row " << i;
    EXPECT_EQ(drawn.y, place.rowY(i)) << "row " << i;
    EXPECT_EQ(drawn.height, props.rowHeight) << "row " << i;
  }
}

}  // namespace

TEST(RowMarksList, PlacesTheSlotOfAListThatFits) { checkAgainstList(4, 0, 0, 0); }

TEST(RowMarksList, PlacesTheSlotOfAnOverflowingListPastTheScrollTrack) { checkAgainstList(20, 0, 0, 0); }

TEST(RowMarksList, PlacesTheSlotOfAScrolledList) { checkAgainstList(20, 5, 0, 0); }

TEST(RowMarksList, PlacesTheSlotWithARowInsetAndALeftTrack) { checkAgainstList(20, 3, 2, 1); }
