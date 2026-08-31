#include <gtest/gtest.h>

#include <fstream>
#include <string>

#include "components/OffsetFieldRow.h"

namespace {

offset_field_row::Widths widths() { return offset_field_row::Widths{40, 24, 32, 6, 36}; }

offset_field_row::Gaps gaps() { return offset_field_row::Gaps{16, 12, 5}; }

offset_field_row::Row row(const int containerX = 0, const int containerWidth = 400) {
  return offset_field_row::layout(widths(), gaps(), containerX, containerWidth, /*y=*/100, /*height=*/28);
}

}  // namespace

TEST(OffsetFieldRowWidth, TheRowIsEveryPartAndEveryGap) {
  EXPECT_EQ(offset_field_row::totalWidth(widths(), gaps()), 40 + 16 + 24 + 12 + 32 + 5 + 6 + 5 + 36);
}

TEST(OffsetFieldRowLayout, TheFieldsRunInReadingOrderWithoutOverlapping) {
  const offset_field_row::Row r = row();
  EXPECT_LE(r.label.x + r.label.width, r.sign.x);
  EXPECT_LE(r.sign.x + r.sign.width, r.hours.x);
  EXPECT_LE(r.hours.x + r.hours.width, r.colon.x);
  EXPECT_LE(r.colon.x + r.colon.width, r.minutes.x);
}

TEST(OffsetFieldRowLayout, EveryGapIsExactlyWhatWasAskedFor) {
  const offset_field_row::Row r = row();
  EXPECT_EQ(r.sign.x - (r.label.x + r.label.width), 16);
  EXPECT_EQ(r.hours.x - (r.sign.x + r.sign.width), 12);
  EXPECT_EQ(r.colon.x - (r.hours.x + r.hours.width), 5);
  EXPECT_EQ(r.minutes.x - (r.colon.x + r.colon.width), 5);
}

TEST(OffsetFieldRowLayout, TheRowIsCentredInTheContainer) {
  const offset_field_row::Row r = row(/*containerX=*/20, /*containerWidth=*/400);
  const int width = offset_field_row::totalWidth(widths(), gaps());
  EXPECT_EQ(r.label.x, 20 + (400 - width) / 2);
  EXPECT_EQ(r.minutes.x + r.minutes.width, r.label.x + width);
}

TEST(OffsetFieldRowLayout, ARowWiderThanTheContainerStartsAtItsLeftEdge) {
  const offset_field_row::Row r = row(/*containerX=*/20, /*containerWidth=*/40);
  EXPECT_EQ(r.label.x, 20);
}

TEST(OffsetFieldRowLayout, EveryPartSharesTheRowsBandAndHeight) {
  const offset_field_row::Row r = row();
  for (const offset_field_row::Rect& part : {r.label, r.sign, r.hours, r.colon, r.minutes}) {
    EXPECT_EQ(part.y, 100);
    EXPECT_EQ(part.height, 28);
  }
}

// Source audit: the offset fields are FreeInkUI buttons and their row comes from
// the header above. The moment the screen paints a field itself, its look stops
// following the theme and this layout stops being what is on the panel.
TEST(ClockOffsetAudit, TheScreenDrawsNoFieldOfItsOwn) {
  std::ifstream file(CLOCK_OFFSET_SOURCE);
  ASSERT_TRUE(file.is_open()) << "cannot open " << CLOCK_OFFSET_SOURCE;
  std::string line;
  int number = 0;
  while (std::getline(file, line)) {
    ++number;
    const std::size_t first = line.find_first_not_of(" \t");
    if (first != std::string::npos && line.compare(first, 2, "//") == 0) continue;
    for (const char* call : {"renderer.drawText", "renderer.drawCenteredText", "renderer.fillRect",
                             "renderer.fillRectDither", "renderer.drawRect", "renderer.getTextWidth"}) {
      EXPECT_EQ(line.find(call), std::string::npos)
          << CLOCK_OFFSET_SOURCE << ":" << number << " calls " << call
          << ". The fields are drawn by FreeInkUI, placed by offset_field_row.";
    }
  }
}
