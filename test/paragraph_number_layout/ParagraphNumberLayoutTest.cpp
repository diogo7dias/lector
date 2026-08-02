// Host tests for where a paragraph number sits against the line it labels.
//
// The metrics below are not invented: they are the real advanceY / ascender / glyph
// ink heights read out of the generated font headers this firmware ships
// (lib/EpdFont/builtinFonts/vollkorn_*_regular.h and spleen_6x12*_regular.h). Pinning
// them here means a change to the placement rule is measured against the fonts the
// device actually draws with, at every reading size the user can pick.
#include <gtest/gtest.h>

#include "ParagraphNumberLayout.h"

namespace {

struct Face {
  int lineHeight;  // advanceY
  int ascender;
  int inkTop;  // 'x' for the body faces, '0' for the number faces
  int inkHeight;
};

// Body: Vollkorn, the built-in reading family, at the four selectable point sizes.
constexpr Face kVollkorn12{35, 24, 12, 12};
constexpr Face kVollkorn14{41, 28, 14, 14};
constexpr Face kVollkorn16{46, 32, 16, 16};
constexpr Face kVollkorn18{52, 36, 18, 18};
// Numbers: Spleen 6x12 on its native cell, and the same cell at exactly 2x.
constexpr Face kSpleenSmall{13, 9, 8, 8};
constexpr Face kSpleenDouble{26, 18, 16, 16};

ParagraphNumberMetrics metricsFor(const Face& body, const Face& num, const int lineTop = 0) {
  ParagraphNumberMetrics m;
  m.lineTop = lineTop;
  m.bodyAscender = body.ascender;
  m.bodyInkTop = body.inkTop;
  m.bodyLineHeight = body.lineHeight;
  m.numAscender = num.ascender;
  m.numInkTop = num.inkTop;
  m.numLineHeight = num.lineHeight;
  return m;
}

// Distance between the centre of the digits and the centre of the body's x-height band.
// Zero means the number is optically centred on the prose. Doubled so the half-pixel
// that integer division can leave is expressible without floating point.
int centreOffsetX2(const Face& body, const Face& num, const int lineTop = 0) {
  const ParagraphNumberMetrics m = metricsFor(body, num, lineTop);
  const int y = paragraphNumberDrawY(m);
  const int numBaseline = y + num.ascender;
  const int bodyBaseline = lineTop + body.ascender;
  const int numCentreX2 = numBaseline * 2 - num.inkHeight;
  const int bodyCentreX2 = bodyBaseline * 2 - body.inkTop;
  return numCentreX2 - bodyCentreX2;
}

}  // namespace

// The whole point of the change: centred, and STAYS centred as the reading size moves.
TEST(ParagraphNumberLayout, DigitsAreCentredOnTheProseAtEveryReadingSize) {
  for (const Face& body : {kVollkorn12, kVollkorn14, kVollkorn16, kVollkorn18}) {
    for (const Face& num : {kSpleenSmall, kSpleenDouble}) {
      EXPECT_EQ(0, centreOffsetX2(body, num)) << "body ascender " << body.ascender << ", num ink " << num.inkTop;
    }
  }
}

// The regression this replaced. Centring the two LINE BOXES left the digits sitting
// above the prose rather than on it, because a declared ascender carries accent room
// that no letter reaches and the two faces carry different amounts of it. Offsets are
// in HALF pixels (see centreOffsetX2), negative meaning the digits ride high.
TEST(ParagraphNumberLayout, LineBoxCentringPutTheDigitsAboveTheProse) {
  const auto boxCentredOffsetX2 = [](const Face& body, const Face& num) {
    const int y = (body.lineHeight - num.lineHeight) / 2;
    const int numCentreX2 = (y + num.ascender) * 2 - num.inkHeight;
    const int bodyCentreX2 = body.ascender * 2 - body.inkTop;
    return numCentreX2 - bodyCentreX2;
  };
  // Double: a flat 4px too high at every reading size — the "stuck to the top" look.
  EXPECT_EQ(-8, boxCentredOffsetX2(kVollkorn12, kSpleenDouble));
  EXPECT_EQ(-8, boxCentredOffsetX2(kVollkorn16, kSpleenDouble));
  EXPECT_EQ(-8, boxCentredOffsetX2(kVollkorn18, kSpleenDouble));
  // Small: high as well, and unlike Double it also drifted with the reading size, so
  // there was no single constant that could have been subtracted to fix both.
  EXPECT_EQ(-4, boxCentredOffsetX2(kVollkorn12, kSpleenSmall));
  EXPECT_EQ(-6, boxCentredOffsetX2(kVollkorn18, kSpleenSmall));
  EXPECT_LT(boxCentredOffsetX2(kVollkorn18, kSpleenSmall), boxCentredOffsetX2(kVollkorn12, kSpleenSmall));
  // Every case was off; the rule under test is exactly 0 for all of them.
  EXPECT_EQ(0, centreOffsetX2(kVollkorn16, kSpleenDouble));
}

TEST(ParagraphNumberLayout, PlacementFollowsTheLineDownThePage) {
  const int first = paragraphNumberDrawY(metricsFor(kVollkorn16, kSpleenDouble, 0));
  const int tenth = paragraphNumberDrawY(metricsFor(kVollkorn16, kSpleenDouble, 460));
  EXPECT_EQ(first + 460, tenth);
}

// A number must never be drawn above the line it labels or below the next one.
TEST(ParagraphNumberLayout, DigitsStayInsideTheLineTheyLabel) {
  for (const Face& body : {kVollkorn12, kVollkorn14, kVollkorn16, kVollkorn18}) {
    for (const Face& num : {kSpleenSmall, kSpleenDouble}) {
      const int y = paragraphNumberDrawY(metricsFor(body, num));
      const int inkTop = y + num.ascender - num.inkTop;
      const int inkBottom = y + num.ascender;
      EXPECT_GE(inkTop, 0) << "digits climbed above the line box";
      EXPECT_LE(inkBottom, body.lineHeight) << "digits dropped into the next line";
    }
  }
}

// A face with no 'x' and no 'H' (or a glyph that failed to load) must still place the
// number somewhere sane rather than at a wild offset.
TEST(ParagraphNumberLayout, FallsBackToLineBoxCentringWithoutInkMetrics) {
  ParagraphNumberMetrics m = metricsFor(kVollkorn16, kSpleenDouble);
  m.bodyInkTop = 0;
  EXPECT_EQ((46 - 26) / 2, paragraphNumberDrawY(m));

  m = metricsFor(kVollkorn16, kSpleenDouble);
  m.numInkTop = 0;
  EXPECT_EQ((46 - 26) / 2, paragraphNumberDrawY(m));
}
