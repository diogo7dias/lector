#include <gtest/gtest.h>

#include "activities/settings/FontPreviewLayout.h"

TEST(FontPreviewLayout, SplitsPreviewIntoTwoBoundedRows) {
  const auto layout = calculateFontPreviewLayout(/*top=*/100, /*height=*/180, /*padding=*/12,
                                                 /*labelHeight=*/18, /*labelGap=*/4, /*rowGap=*/6);

  EXPECT_EQ(layout.normal.labelY, 112);
  EXPECT_EQ(layout.normal.textTop, 134);
  EXPECT_LT(layout.normal.textTop, layout.normal.textBottom);
  EXPECT_EQ(layout.paperback.labelY, layout.normal.textBottom + 6);
  EXPECT_EQ(layout.paperback.textTop, layout.paperback.labelY + 22);
  EXPECT_LE(layout.paperback.textBottom, 268);
}

TEST(FontPreviewLayout, GivesBothRowsEqualTextHeight) {
  const auto layout = calculateFontPreviewLayout(/*top=*/0, /*height=*/201, /*padding=*/10,
                                                 /*labelHeight=*/16, /*labelGap=*/3, /*rowGap=*/5);

  EXPECT_EQ(layout.normal.textBottom - layout.normal.textTop,
            layout.paperback.textBottom - layout.paperback.textTop);
}

TEST(FontPreviewLayout, CollapsesSafelyWhenPaneIsTooShort) {
  const auto layout = calculateFontPreviewLayout(/*top=*/20, /*height=*/30, /*padding=*/12,
                                                 /*labelHeight=*/18, /*labelGap=*/4, /*rowGap=*/6);

  EXPECT_GE(layout.normal.textBottom, layout.normal.textTop);
  EXPECT_GE(layout.paperback.textBottom, layout.paperback.textTop);
  EXPECT_LE(layout.paperback.textBottom, 38);
}
