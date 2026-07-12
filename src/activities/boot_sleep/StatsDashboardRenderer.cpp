#include "StatsDashboardRenderer.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <strings.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "Epub/converters/DirectPixelWriter.h"
#include "fontIds.h"
#include "reading_stats/ReadingStatsPresentation.h"

namespace stats_dashboard {
namespace {

using reading_stats::DashboardImageRect;
using reading_stats::DashboardLayout;
using reading_stats::ReadingStatsData;

constexpr int kCoverRadius = 8;
constexpr int kTitleTopGap = 28;
constexpr int kTitleChapterGap = 8;
constexpr int kStatsRows = 7;
constexpr int kStatsValueLabelGap = 1;

bool isPxc(const std::string& path) {
  return path.size() >= 4 && strcasecmp(path.c_str() + path.size() - 4, ".pxc") == 0;
}

void drawRightText(const GfxRenderer& renderer, const int font, const int rightX, const int y, const char* text,
                   const bool bold = false) {
  const EpdFontFamily::Style style = bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
  renderer.drawText(font, rightX - renderer.getTextWidth(font, text, style), y, text, true, style);
}

const char* readerTypeLabel(const ReadingStatsData& global) {
  const uint32_t total =
      global.timeOfDaySeconds[0] + global.timeOfDaySeconds[1] + global.timeOfDaySeconds[2] + global.timeOfDaySeconds[3];
  if (total == 0) return tr(STR_STATS_NEW_READER);
  switch (reading_stats::dominantTimeOfDay(global.timeOfDaySeconds)) {
    case reading_stats::TimeOfDay::Morning:
      return tr(STR_STATS_MORNING_READER);
    case reading_stats::TimeOfDay::Afternoon:
      return tr(STR_STATS_AFTERNOON_READER);
    case reading_stats::TimeOfDay::Evening:
      return tr(STR_STATS_EVENING_READER);
    case reading_stats::TimeOfDay::Night:
      return tr(STR_STATS_NIGHT_READER);
  }
  return tr(STR_STATS_MORNING_READER);
}

void drawStatsRow(const GfxRenderer& renderer, const int rightX, const int y, const std::string& value,
                  const std::string& label) {
  const int valueH = renderer.getLineHeight(UI_12_FONT_ID);
  drawRightText(renderer, UI_12_FONT_ID, rightX, y, value.c_str(), true);
  drawRightText(renderer, SMALL_FONT_ID, rightX, y + valueH + kStatsValueLabelGap, label.c_str());
}

std::string formatRate(const ReadingStatsData& stats) {
  char value[16];
  snprintf(value, sizeof(value), "%.1f",
           reading_stats::pagesPerMinute(stats.totalPagesTurned, stats.totalReadingSeconds));
  return value;
}

void drawChrome(const GfxRenderer& renderer, const DashboardData& data, const DashboardLayout& layout,
                const DashboardImageRect& imageRect) {
  const DashboardImageRect cover{layout.coverX, layout.coverY, layout.coverWidth, layout.coverHeight};
  renderer.maskRoundedRectOutsideCorners(imageRect.x, imageRect.y, imageRect.width, imageRect.height, kCoverRadius,
                                         Color::White);
  renderer.drawRoundedRect(imageRect.x, imageRect.y, imageRect.width, imageRect.height, 1, kCoverRadius, true);

  const uint32_t timeLeft = data.book.estimatedTimeLeftSeconds > 0
                                ? data.book.estimatedTimeLeftSeconds
                                : reading_stats::estimateTimeLeft(data.book.totalReadingSeconds, data.progressPercent);
  const uint32_t dailyAverage =
      reading_stats::averagePerObservedDay(data.book.totalReadingSeconds, data.book.startDay, data.todayDay);
  const uint32_t finishDay = data.book.completed
                                 ? data.book.finishedDay
                                 : reading_stats::estimateFinishDay(data.todayDay, data.book.startDay,
                                                                    data.book.totalReadingSeconds, timeLeft);
  const uint32_t daysReading =
      data.book.startDay != 0 && data.todayDay >= data.book.startDay ? data.todayDay - data.book.startDay + 1u : 0;

  char progress[12];
  snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(data.progressPercent));
  char days[20];
  snprintf(days, sizeof(days), "%lu %s", static_cast<unsigned long>(daysReading), daysReading == 1 ? "day" : "days");
  const std::string startedLabel =
      std::string(tr(STR_STATS_STARTED)) + " " + reading_stats::formatShortDate(data.book.startDay);
  const std::array<std::pair<std::string, std::string>, kStatsRows> rows = {{
      {reading_stats::formatDuration(data.book.totalReadingSeconds), tr(STR_STATS_READING_TIME)},
      {reading_stats::formatDuration(timeLeft), tr(STR_STATS_TIME_LEFT)},
      {progress, tr(STR_STATS_PROGRESS)},
      {reading_stats::formatDuration(dailyAverage), tr(STR_STATS_DAILY_AVG)},
      {formatRate(data.book), tr(STR_STATS_PAGES_PER_MIN)},
      {daysReading > 0 ? days : "-", startedLabel},
      {reading_stats::formatShortDate(finishDay),
       data.book.completed ? tr(STR_STATS_FINISHED) : tr(STR_STATS_EST_FINISH)},
  }};

  const int blockH =
      renderer.getLineHeight(UI_12_FONT_ID) + kStatsValueLabelGap + renderer.getLineHeight(SMALL_FONT_ID);
  const int remaining = std::max(0, cover.height - blockH * kStatsRows);
  const int gap = remaining / (kStatsRows - 1);
  const int remainder = remaining % (kStatsRows - 1);
  for (int index = 0; index < kStatsRows; ++index) {
    const int y = cover.y + index * (blockH + gap) + std::min(index, remainder);
    drawStatsRow(renderer, layout.statsRightX, y, rows[index].first, rows[index].second);
  }

  const int textWidth = renderer.getScreenWidth() - cover.x * 2;
  int textY = cover.y + cover.height + kTitleTopGap;
  const auto titleLines = renderer.wrappedText(UI_12_FONT_ID, data.title.c_str(), textWidth, 2, EpdFontFamily::BOLD);
  for (const auto& line : titleLines) {
    renderer.drawText(UI_12_FONT_ID, cover.x, textY, line.c_str(), true, EpdFontFamily::BOLD);
    textY += renderer.getLineHeight(UI_12_FONT_ID);
  }
  if (!data.chapter.empty()) {
    textY += kTitleChapterGap;
    const auto chapterLines = renderer.wrappedText(UI_10_FONT_ID, data.chapter.c_str(), textWidth, 2);
    for (const auto& line : chapterLines) {
      renderer.drawText(UI_10_FONT_ID, cover.x, textY, line.c_str());
      textY += renderer.getLineHeight(UI_10_FONT_ID);
    }
  }

  char streak[64];
  const uint16_t currentStreak = data.global.currentStreak(data.todayDay);
  if (currentStreak == 0) {
    snprintf(streak, sizeof(streak), "%s", tr(STR_STATS_NO_STREAK));
  } else {
    snprintf(streak, sizeof(streak), tr(STR_STATS_DAY_STREAK_FORMAT), static_cast<unsigned>(currentStreak));
  }
  renderer.drawText(UI_10_FONT_ID, cover.x, layout.footerY, streak, true, EpdFontFamily::BOLD);

  drawRightText(renderer, UI_10_FONT_ID, layout.statsRightX, layout.footerY, readerTypeLabel(data.global), true);
}

bool renderBitmap(GfxRenderer& renderer, const DashboardData& data, Bitmap& bitmap, const DashboardLayout& layout,
                  const DashboardImageRect& imageRect) {
  auto drawImage = [&]() {
    bitmap.rewindToData();
    renderer.drawBitmap(bitmap, imageRect.x, imageRect.y, imageRect.width, imageRect.height);
  };

  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  drawImage();
  drawChrome(renderer, data, layout, imageRect);

  if (!bitmap.hasGreyscale()) {
    renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    return true;
  }

  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);
  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  drawImage();
  drawChrome(renderer, data, layout, imageRect);
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  drawImage();
  drawChrome(renderer, data, layout, imageRect);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

bool renderPxc(GfxRenderer& renderer, const DashboardData& data, HalFile& file, const uint16_t sourceWidth,
               const uint16_t sourceHeight, const size_t dataOffset, const DashboardLayout& layout,
               const DashboardImageRect& imageRect) {
  const int bytesPerRow = (sourceWidth + 3) / 4;
  auto rowBuffer = makeUniqueNoThrow<uint8_t[]>(bytesPerRow);
  if (!rowBuffer) return false;

  auto decode = [&]() -> bool {
    if (!file.seek(dataOffset)) return false;
    DirectPixelWriter writer;
    writer.init(renderer);
    int nextDestinationRow = 0;
    for (int sourceRow = 0; sourceRow < sourceHeight; ++sourceRow) {
      if (file.read(rowBuffer.get(), bytesPerRow) != bytesPerRow) return false;
      while (nextDestinationRow < imageRect.height &&
             reading_stats::mapDashboardPixel(nextDestinationRow, imageRect.height, sourceHeight) == sourceRow) {
        writer.beginRow(imageRect.y + nextDestinationRow);
        int colStart = 0;
        int colEnd = imageRect.width;
        writer.bandColRange(imageRect.x, imageRect.width, colStart, colEnd);
        for (int destinationColumn = colStart; destinationColumn < colEnd; ++destinationColumn) {
          const int sourceColumn = reading_stats::mapDashboardPixel(destinationColumn, imageRect.width, sourceWidth);
          const int byteIndex = sourceColumn >> 2;
          const int shift = 6 - (sourceColumn & 3) * 2;
          const uint8_t value = (rowBuffer[byteIndex] >> shift) & 0x03;
          writer.writePixel(imageRect.x + destinationColumn, value);
        }
        ++nextDestinationRow;
      }
    }
    return nextDestinationRow == imageRect.height;
  };

  renderer.clearScreen();
  renderer.setRenderMode(GfxRenderer::BW);
  if (!decode()) return false;
  drawChrome(renderer, data, layout, imageRect);
  renderer.displayGrayscaleBase(HalDisplay::HALF_REFRESH);

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
  if (!decode()) return false;
  drawChrome(renderer, data, layout, imageRect);
  renderer.copyGrayscaleLsbBuffers();

  renderer.clearScreen(0x00);
  renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
  if (!decode()) return false;
  drawChrome(renderer, data, layout, imageRect);
  renderer.copyGrayscaleMsbBuffers();
  renderer.displayGrayBuffer();
  renderer.setRenderMode(GfxRenderer::BW);
  return true;
}

}  // namespace

bool render(GfxRenderer& renderer, const DashboardData& data) {
  if (data.imagePath.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead("DASH", data.imagePath, file)) return false;

  const DashboardLayout layout = reading_stats::dashboardLayout(renderer.getScreenWidth(), renderer.getScreenHeight());
  const DashboardImageRect target{layout.coverX, layout.coverY, layout.coverWidth, layout.coverHeight};

  bool rendered = false;
  if (isPxc(data.imagePath)) {
    uint16_t width = 0;
    uint16_t height = 0;
    if (file.read(&width, 2) == 2 && file.read(&height, 2) == 2 && width > 0 && height > 0 &&
        abs(static_cast<int>(width) - renderer.getScreenWidth()) <= 1 &&
        abs(static_cast<int>(height) - renderer.getScreenHeight()) <= 1) {
      const DashboardImageRect imageRect = reading_stats::fitDashboardImage(width, height, target);
      rendered = renderPxc(renderer, data, file, width, height, file.position(), layout, imageRect);
    }
  } else {
    Bitmap bitmap(file, true);
    if (bitmap.parseHeaders() == BmpReaderError::Ok) {
      const DashboardImageRect imageRect =
          reading_stats::fitDashboardImage(bitmap.getWidth(), bitmap.getHeight(), target);
      rendered = renderBitmap(renderer, data, bitmap, layout, imageRect);
    }
  }
  file.close();
  if (!rendered) renderer.setRenderMode(GfxRenderer::BW);
  return rendered;
}

}  // namespace stats_dashboard
