// The Quote sleep face: one highlight from /My Clippings.txt, the same one all day when
// the clock is set, set in the reading font between two rules with its book under it.
#include <ClockZones.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalStorage.h>
#include <esp_random.h>

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "SleepActivity.h"
#include "SleepFacePaint.h"
#include "SleepInfoOverlay.h"
#include "SleepTiming.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/ClippingsReader.h"
#include "util/QuoteText.h"

void SleepActivity::renderQuoteSleepScreen() const {
  clippings::Record rec;
  bool found = false;
  {
    HalFile file;
    if (Storage.openFileForRead("SLP", quote_text::CLIPPINGS_PATH, file)) {
      const uint64_t fileSize = file.fileSize64();
      if (fileSize > 0) {
        uint64_t seed = 0;
        uint16_t year = 0;
        uint8_t month = 0, day = 0, hour = 0, minute = 0;
        if (halClock.getDateTime(year, month, day, hour, minute)) {
          const int64_t utc = clock_zones::utcSeconds(year, month, day, hour, minute);
          const time_t local = static_cast<time_t>(
              utc + clock_zones::offsetMinutes(SETTINGS.clockTimezone, SETTINGS.clockDst, utc) * 60);
          tm t{};
          gmtime_r(&local, &t);
          seed = clippings::dailySeed(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
        } else {
          seed = (static_cast<uint64_t>(esp_random()) << 32) | esp_random();
        }
        auto readChunk = [](void* ctx, const uint64_t offset, char* dst, const size_t len) -> size_t {
          auto* f = static_cast<HalFile*>(ctx);
          if (!f->seek64(offset)) return 0;
          const int n = f->read(dst, len);
          return n > 0 ? static_cast<size_t>(n) : 0;
        };
        found = clippings::pickAtOffset(fileSize, clippings::offsetForSeed(seed, fileSize), readChunk, &file, rec);
      }
    }
  }

  if (!found) {
    if (!APP_STATE.openEpubPath.empty()) return renderCoverSleepScreen();
    return renderDefaultSleepScreen();
  }

  // Collapse internal line breaks into single spaces so wrappedText typesets cleanly.
  std::string body = rec.quote;
  for (char& c : body) {
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  }

  int fontId = SETTINGS.getReaderFontId();
  if (renderer.getLineHeight(fontId) <= 0) fontId = CHAREINK_14_FONT_ID;

  renderer.prewarmFallbackText(fontId, body.c_str());
  renderer.prewarmFallbackText(UI_10_FONT_ID, rec.title.c_str());
  if (!rec.author.empty()) renderer.prewarmFallbackText(UI_10_FONT_ID, rec.author.c_str());

  int viewTop = 0, viewRight = 0, viewBottom = 0, viewLeft = 0;
  renderer.getOrientedViewableTRBL(&viewTop, &viewRight, &viewBottom, &viewLeft);
  const int pageW = renderer.getScreenWidth();
  const int pageH = renderer.getScreenHeight();
  const int padX = std::max(36, pageW / 10);
  const int contentX = viewLeft + padX;
  const int contentW = std::max(80, pageW - viewLeft - viewRight - padX * 2);
  const int usableTop = viewTop + 32;
  const int usableH = std::max(120, pageH - viewTop - viewBottom - 64);

  const int quoteLineH = std::max(16, renderer.getLineHeight(fontId) + 4);
  const int attrLineH = std::max(14, renderer.getLineHeight(UI_10_FONT_ID) + 2);
  constexpr int kRuleH = 2;
  constexpr int kRuleGap = 24;
  constexpr int kAttrGap = 18;

  const auto titleLines = renderer.wrappedText(UI_10_FONT_ID, rec.title.c_str(), contentW, 2, EpdFontFamily::BOLD);
  const auto authorLines = rec.author.empty() ? std::vector<std::string>{}
                                              : renderer.wrappedText(UI_10_FONT_ID, rec.author.c_str(), contentW, 1);

  const int chromeH = kRuleH * 2 + kRuleGap * 2 + kAttrGap + static_cast<int>(titleLines.size()) * attrLineH +
                      static_cast<int>(authorLines.size()) * attrLineH;
  const int maxQuoteLines = std::max(1, (usableH - chromeH) / quoteLineH);
  const auto quoteLines = renderer.wrappedText(fontId, body.c_str(), contentW, maxQuoteLines);

  const int blockH = chromeH + static_cast<int>(quoteLines.size()) * quoteLineH;
  int y = usableTop + std::max(0, (usableH - blockH) / 2);

  renderer.clearScreen();
  const int ruleW = std::min(contentW, std::max(96, contentW / 2));
  const int ruleX = contentX + (contentW - ruleW) / 2;
  renderer.fillRect(ruleX, y, ruleW, kRuleH, true);
  y += kRuleH + kRuleGap;

  for (const auto& line : quoteLines) {
    renderer.drawText(fontId, contentX, y, line.c_str(), true);
    y += quoteLineH;
  }

  y += kRuleGap;
  renderer.fillRect(ruleX, y, ruleW, kRuleH, true);
  y += kRuleH + kAttrGap;

  const Rect area{contentX, usableTop, contentW, usableH};
  for (const auto& line : titleLines) {
    UITheme::drawCenteredText(renderer, area, UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::BOLD);
    y += attrLineH;
  }
  for (const auto& line : authorLines) {
    UITheme::drawCenteredText(renderer, area, UI_10_FONT_ID, y, line.c_str(), true, EpdFontFamily::REGULAR);
    y += attrLineH;
  }

  drawSleepInfoOverlay(renderer);
  SleepTiming::mark("decode");
  const sleep_face::PaintPlan plan =
      sleep_face::planFor(sleep_face::Face::PlainLector, /*sourceHasGrayscale=*/false, display.profile());
  renderer.displayBuffer(plan.base);
}
