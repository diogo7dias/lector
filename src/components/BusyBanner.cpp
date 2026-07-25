#include "BusyBanner.h"

#include <Arduino.h>
#include <GfxRenderer.h>

#include "UITheme.h"
#include "fontIds.h"
#include "util/BusyTick.h"

namespace {

// The innermost live banner, so the bare function-pointer tick handler can reach
// it. Banners nest (opening a book loads a font, then builds an index), so each
// one remembers its predecessor and puts it back on the way out rather than
// leaving the handler pointing at a destroyed object.
BusyBanner* g_activeBanner = nullptr;

constexpr int PADDING_X = 10;
constexpr int PADDING_Y = 5;

}  // namespace

BusyBanner::BusyBanner(GfxRenderer& renderer, const char* text, const uint32_t delayMs)
    : renderer(renderer), text(text), delayMs(delayMs), startMs(millis()), previous(g_activeBanner) {
  g_activeBanner = this;
  busy::setTickHandler(&BusyBanner::tickTrampoline, &BusyBanner::tickNowTrampoline);
}

BusyBanner::~BusyBanner() {
  g_activeBanner = previous;
  if (previous == nullptr) busy::setTickHandler(nullptr, nullptr);
}

void BusyBanner::tickTrampoline() {
  if (g_activeBanner != nullptr) g_activeBanner->onTick();
}

void BusyBanner::tickNowTrampoline() {
  if (g_activeBanner != nullptr) g_activeBanner->showNow();
}

void BusyBanner::onTick() {
  if (drawn) return;
  if (millis() - startMs < delayMs) return;
  showNow();
}

void BusyBanner::showNow() {
  if (drawn) return;
  drawn = true;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int height = lineHeight + PADDING_Y * 2;
  // Starts at topPadding, not y=0: that padding is what keeps chrome clear of the
  // X4's physical top crop, and a banner drawn above it loses its first rows.
  const int top = metrics.topPadding;

  // Opaque, because the banner lands on top of whatever screen the user was
  // looking at. The rule underneath separates it from that content.
  renderer.fillRect(0, top, width, height, false);
  renderer.drawRect(0, top + height, width, 1, 1, true);
  renderer.drawText(UI_10_FONT_ID, PADDING_X, top + PADDING_Y, text, true, EpdFontFamily::REGULAR);

  // FAST is the cheapest waveform the panel has, which matters because this
  // refresh is pure overhead added to work the user is already waiting on.
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
}
