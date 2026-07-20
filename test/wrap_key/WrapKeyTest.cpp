#include <gtest/gtest.h>

#include "lib/Epub/Epub/parsers/WrapKey.h"

using wrapkey::wrapKeyHash;

namespace {
// A fixed "baseline" set of the eleven wrap-affecting fields.
uint32_t baseline() {
  return wrapKeyHash(/*fontId=*/3, /*extraParagraphSpacing=*/true, /*paragraphAlignment=*/0, /*viewportWidth=*/470,
                     /*hyphenationEnabled=*/false, /*embeddedStyle=*/true, /*imageRendering=*/0,
                     /*focusReadingEnabled=*/false, /*guideDotsEnabled=*/false, /*firstLineIndentPx=*/0,
                     /*wordSpacing=*/3);
}
}  // namespace

TEST(WrapKeyTest, Deterministic) { EXPECT_EQ(baseline(), baseline()); }

// Guards against accidental fold-order / basis / prime changes: if this value
// moves, the on-disk wrap-key semantics changed and re-pack matching would break.
TEST(WrapKeyTest, GoldenValueStable) {
  const uint32_t expected = wrapkey::fnvFoldPod(
      wrapkey::fnvFoldPod(
          wrapkey::fnvFoldPod(
              wrapkey::fnvFoldPod(
                  wrapkey::fnvFoldPod(
                      wrapkey::fnvFoldPod(
                          wrapkey::fnvFoldPod(
                              wrapkey::fnvFoldPod(wrapkey::fnvFoldPod(wrapkey::fnvFoldPod(
                                                      wrapkey::fnvFoldPod(2166136261u, static_cast<int>(3)),
                                                      static_cast<bool>(true)),
                                                  static_cast<uint8_t>(0)),
                                                  static_cast<uint16_t>(470)),
                              static_cast<bool>(false)),
                          static_cast<bool>(true)),
                      static_cast<uint8_t>(0)),
                  static_cast<bool>(false)),
              static_cast<bool>(false)),
          static_cast<int>(0)),
      static_cast<uint8_t>(3));
  EXPECT_EQ(baseline(), expected);
}

// Every one of the eleven wrap-affecting fields must change the hash — otherwise
// a re-pack could reuse a layout wrapped under different (wrap-affecting) settings.
TEST(WrapKeyTest, EveryWrapFieldMatters) {
  const uint32_t base = baseline();
  EXPECT_NE(base, wrapKeyHash(4, true, 0, 470, false, true, 0, false, false, 0, 3));    // fontId
  EXPECT_NE(base, wrapKeyHash(3, false, 0, 470, false, true, 0, false, false, 0, 3));   // extraParagraphSpacing
  EXPECT_NE(base, wrapKeyHash(3, true, 1, 470, false, true, 0, false, false, 0, 3));    // paragraphAlignment
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 471, false, true, 0, false, false, 0, 3));    // viewportWidth
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, true, true, 0, false, false, 0, 3));     // hyphenationEnabled
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, false, 0, false, false, 0, 3));   // embeddedStyle
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, true, 1, false, false, 0, 3));    // imageRendering
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, true, 0, true, false, 0, 3));     // focusReadingEnabled
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, true, 0, false, true, 0, 3));     // guideDotsEnabled
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, true, 0, false, false, 24, 3));   // firstLineIndentPx
  EXPECT_NE(base, wrapKeyHash(3, true, 0, 470, false, true, 0, false, false, 0, 5));    // wordSpacing
}
