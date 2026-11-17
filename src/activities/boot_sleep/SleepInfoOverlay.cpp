#include "SleepInfoOverlay.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"
#include "util/FavoriteImage.h"
#include "util/ReadingJournalStore.h"

namespace {

// The wallpaper currently being rendered, owned by SleepInfoOverlayScope. Empty
// means "no wallpaper in scope", which disables the overlay entirely.
std::string g_sourcePath;
// Rotation line position of that wallpaper ("N of M this loop"); 0/0 = unknown
// (fixed sleep file, jump-pick fallback), which hides the position badge.
uint32_t g_position = 0;
uint32_t g_total = 0;
// The reading band's rows, each two values ("1h19", "127 pages"); an empty time hides
// that row. The sitting row is the one the reader just closed, above the week.
char g_sittingTime[16] = "";
char g_sittingPages[24] = "";
char g_readingTime[16] = "";
char g_readingPages[24] = "";
// The sleep line needs one week; even a heavy week is a few KB of journal.
constexpr size_t READING_TAIL_BYTES = 16 * 1024;
constexpr int BAND_PADDING_Y = 4;
constexpr int BAND_RULE = 2;

int readingBandRows() { return (g_sittingTime[0] ? 1 : 0) + (g_readingTime[0] ? 1 : 0); }

int readingBandHeight(const GfxRenderer& renderer) {
  const int rows = readingBandRows();
  return rows ? rows * (renderer.getLineHeight(UI_10_FONT_ID) + 2 * BAND_PADDING_Y) + BAND_RULE : 0;
}

enum class Corner { BottomLeft, BottomRight };

// Filled box + text in a bottom safe corner. White text on a black box so it
// reads over any wallpaper. Both badges share this so the filename, the "F"
// mark and the rotation position are the same shape on screen.
void drawLabel(const GfxRenderer& renderer, const std::string& text, const Corner corner = Corner::BottomLeft) {
  if (text.empty()) return;
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  constexpr int safeInset = 18;
  constexpr int paddingX = 4;
  constexpr int paddingY = 2;
  const int textLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int maxBoxWidth = std::max(1, screenWidth - safeInset * 2);
  const int maxTextWidth = std::max(1, maxBoxWidth - paddingX * 2 - 2);

  const std::string shown = renderer.truncatedText(UI_10_FONT_ID, text.c_str(), maxTextWidth);
  const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, shown.c_str(), EpdFontFamily::REGULAR);
  const int boxWidth = std::min(textWidth + paddingX * 2, maxBoxWidth);
  const int boxHeight = textLineHeight + paddingY * 2;
  // The position badge hugs the right edge and the band's top border; the left label keeps the safe inset.
  constexpr int rightInset = 4;
  const int inset = corner == Corner::BottomRight ? rightInset : safeInset;
  const int boxX =
      corner == Corner::BottomRight ? std::max(rightInset, screenWidth - rightInset - boxWidth) : safeInset;
  // Above the reading band, when there is one, rather than over it.
  const int boxY = std::max(inset, screenHeight - readingBandHeight(renderer) - boxHeight - inset);

  // The black box must be drawn in EVERY pass: in the grayscale plane passes
  // fillRect(true) clears the plane bits over the box area, which is what stops
  // the wallpaper's gray nudges from bleeding through it. The white text and
  // border must be drawn ONLY in the BW base pass: the 1-bit glyph/rect path
  // ignores the render mode and would set the LSB+MSB plane bits (the dark-grey
  // nudge cell), turning the white pixels dark grey on 3-pass wallpapers.
  renderer.fillRect(boxX, boxY, boxWidth, boxHeight, true);
  if (renderer.getRenderMode() == GfxRenderer::BW) {
    renderer.drawRect(boxX, boxY, boxWidth, boxHeight, 2, false);
    renderer.drawText(UI_10_FONT_ID, boxX + paddingX, boxY + paddingY, shown.c_str(), false, EpdFontFamily::REGULAR);
  }
}

}  // namespace

SleepInfoOverlayScope::SleepInfoOverlayScope(const std::string& sourcePath, const uint32_t position,
                                             const uint32_t total) {
  g_sourcePath = sourcePath;
  g_position = position;
  g_total = total;
}

SleepInfoOverlayScope::~SleepInfoOverlayScope() {
  g_sourcePath.clear();
  g_position = 0;
  g_total = 0;
}

namespace {

// One row of the band, "Last 7 days  1h19  127 pages", in white on the black band.
void drawReadingRow(const GfxRenderer& renderer, const int ty, const char* label, const char* time, const char* pages) {
  constexpr int gap = 18;
  const int w = renderer.getScreenWidth();
  const int labelW = renderer.getTextWidth(UI_10_FONT_ID, label, EpdFontFamily::BOLD);
  const int timeW = renderer.getTextWidth(UI_10_FONT_ID, time, EpdFontFamily::BOLD);
  const int pagesW = renderer.getTextWidth(UI_10_FONT_ID, pages);
  // Left and right keep well clear of the edge: the bezel hides the outer pixels on the device.
  constexpr int edgeInset = 32;
  const int textW = labelW + timeW + pagesW + 2 * gap;
  int x = (w - textW) / 2;
  if (SETTINGS.sleepReadingStatsAlign == CrossPointSettings::SLEEP_STATS_LEFT) x = edgeInset;
  if (SETTINGS.sleepReadingStatsAlign == CrossPointSettings::SLEEP_STATS_RIGHT) x = w - edgeInset - textW;
  x = std::max(0, x);
  renderer.drawText(UI_10_FONT_ID, x, ty, label, false, EpdFontFamily::BOLD);
  x += labelW + gap;
  renderer.drawText(UI_10_FONT_ID, x, ty, time, false, EpdFontFamily::BOLD);
  x += timeW + gap;
  renderer.drawText(UI_10_FONT_ID, x, ty, pages, false);
}

// A thin black band across the foot, white text under a 2 px white rule: this sitting,
// when the reader just closed one, above the last seven days.
void drawReadingBand(const GfxRenderer& renderer) {
  const int bandH = readingBandHeight(renderer);
  if (bandH == 0) return;
  const int w = renderer.getScreenWidth();
  const int y = renderer.getScreenHeight() - bandH;
  // Black in every pass and white rule and text in the BW pass only, for the reasons in drawLabel.
  renderer.fillRect(0, y, w, bandH, true);
  if (renderer.getRenderMode() != GfxRenderer::BW) return;
  renderer.fillRect(0, y, w, BAND_RULE, false);

  const int rowH = renderer.getLineHeight(UI_10_FONT_ID) + 2 * BAND_PADDING_Y;
  int ty = y + BAND_RULE + BAND_PADDING_Y;
  if (g_sittingTime[0]) {
    drawReadingRow(renderer, ty, tr(STR_STATS_THIS_SITTING), g_sittingTime, g_sittingPages);
    ty += rowH;
  }
  if (g_readingTime[0]) drawReadingRow(renderer, ty, tr(STR_STATS_LAST_7_DAYS), g_readingTime, g_readingPages);
}

}  // namespace

void prepareSleepReadingLine() {
  clearSleepReadingLine();
  if (!SETTINGS.showSleepReadingStats) return;
  // Only when this lock left a book: locking from Home would otherwise show a sitting that
  // ended long before. The sitting needs no clock, so it shows even when the week cannot.
  uint32_t seconds = 0, pages = 0;
  if (APP_STATE.lastSleepFromReader && reading_journal::lastSitting(seconds, pages)) {
    reading_journal::formatDuration(g_sittingTime, sizeof(g_sittingTime), seconds);
    snprintf(g_sittingPages, sizeof(g_sittingPages), tr(STR_STATS_PAGES), static_cast<unsigned long>(pages));
  }
  reading_journal::Summary summary;
  if (reading_journal::summarize(summary, READING_TAIL_BYTES) && summary.week.sessions > 0) {
    reading_journal::formatDuration(g_readingTime, sizeof(g_readingTime), summary.week.seconds);
    snprintf(g_readingPages, sizeof(g_readingPages), tr(STR_STATS_PAGES),
             static_cast<unsigned long>(summary.week.pages));
  }
  LOG_DBG("SLP", "Reading band: sitting %s %s, week %s %s", g_sittingTime, g_sittingPages, g_readingTime,
          g_readingPages);
}

void clearSleepReadingLine() {
  g_sittingTime[0] = '\0';
  g_sittingPages[0] = '\0';
  g_readingTime[0] = '\0';
  g_readingPages[0] = '\0';
}

void drawSleepInfoOverlay(GfxRenderer& renderer) {
  drawReadingBand(renderer);
  if (g_sourcePath.empty()) return;

  if (SETTINGS.showSleepImageFilename) {
    drawLabel(renderer, FavoriteImage::displayNameForPath(g_sourcePath));
  } else if (SETTINGS.showSleepFavoriteBadge && FavoriteImage::isFavoritePath(g_sourcePath)) {
    // Just "F" — the box border reads as the brackets.
    drawLabel(renderer, "F");
  }
  if (SETTINGS.showSleepWallpaperPosition && g_total > 0 && g_position > 0) {
    // Same boxed shape as the "F" mark, mirrored to the other corner.
    char text[24];
    snprintf(text, sizeof(text), "%lu / %lu", static_cast<unsigned long>(g_position),
             static_cast<unsigned long>(g_total));
    drawLabel(renderer, text, Corner::BottomRight);
  }
}
