#include "SpeedTestActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <PerfStats.h>

#include <cstdio>

#include "fontIds.h"

extern HalDisplay display;

namespace {
constexpr const char* kSample[] = {
    "The quick brown fox jumps over the dog,",
    "turns the page and reads on. Ink is slow;",
    "every line here changes on each pass,",
    "as a real page turn would. 0123456789",
};
constexpr int kSampleCount = sizeof(kSample) / sizeof(kSample[0]);
}  // namespace

void SpeedTestActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SpeedTestActivity::loop() {
  int x = 0;
  int y = 0;
  if (done && (mappedInput.wasAnyPressed() || mappedInput.wasScreenTapped(x, y))) finish();
}

void SpeedTestActivity::render(RenderLock&&) {
  if (!done) {
    runBench();
    done = true;
  }
  drawResults();
}

void SpeedTestActivity::drawTestPage(const bool shifted) const {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, 10, 36, tr(STR_SPEED_TEST_RUNNING), true);
  for (int row = 0, y = 46 + 2 * lh; y + lh < renderer.getScreenHeight(); ++row, y += lh) {
    renderer.drawText(UI_12_FONT_ID, shifted ? 18 : 10, y, kSample[(row + (shifted ? 1 : 0)) % kSampleCount], true);
  }
}

SpeedTestActivity::Pass SpeedTestActivity::refresh(const HalDisplay::RefreshMode mode) const {
  renderer.displayBuffer(mode);
  Pass p;
  PerfStats::lastRefresh(p.mode, p.totalUs, p.wireUs, p.waveUs);
  return p;
}

void SpeedTestActivity::runBench() {
  const uint32_t t0 = micros();
  drawTestPage(false);
  drawUs = micros() - t0;
  refresh(HalDisplay::HALF_REFRESH);  // a clean baseline, so every turn starts from the same glass
  for (int i = 0; i < kTurns; ++i) {
    drawTestPage(i % 2 == 0);
    turns[i] = refresh(HalDisplay::FAST_REFRESH);
  }
  drawTestPage(false);
  clean = refresh(HalDisplay::HALF_REFRESH);
  drawTestPage(true);
  full = refresh(HalDisplay::FULL_REFRESH);
  LOG_INF("SPEED", "draw %lu us, half %lu us, full %lu us", static_cast<unsigned long>(drawUs),
          static_cast<unsigned long>(clean.totalUs), static_cast<unsigned long>(full.totalUs));
}

void SpeedTestActivity::drawResults() const {
  // Page turns the anti-ghost policy promoted ran a slower waveform: averaged apart.
  uint32_t n = 0, sum = 0, minMs = UINT32_MAX, maxMs = 0, wire = 0, wave = 0;
  for (const Pass& t : turns) {
    if (t.mode != HalDisplay::FAST_REFRESH) continue;
    const uint32_t ms = t.totalUs / 1000;
    ++n;
    sum += ms;
    wire += t.wireUs / 1000;
    wave += t.waveUs / 1000;
    if (ms < minMs) minMs = ms;
    if (ms > maxMs) maxMs = ms;
  }

  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID) + 6;
  int y = 40 + lh;  // below the speed line and the title
  char line[80];
  const auto put = [&](const char* text) {
    renderer.drawText(UI_12_FONT_ID, 16, y, text, true);
    y += lh;
  };
  renderer.drawCenteredText(UI_12_FONT_ID, 36, tr(STR_SPEED_TEST), true);
  if (n > 0) {
    snprintf(line, sizeof(line), "%s: %lu ms (%lu-%lu)", tr(STR_SPEED_PAGE_TURN), static_cast<unsigned long>(sum / n),
             static_cast<unsigned long>(minMs), static_cast<unsigned long>(maxMs));
    put(line);
    snprintf(line, sizeof(line), "  wire %lu  wave %lu  other %lu", static_cast<unsigned long>(wire / n),
             static_cast<unsigned long>(wave / n), static_cast<unsigned long>((sum - wire - wave) / n));
    put(line);
  }
  snprintf(line, sizeof(line), "%s: %lu of %d", tr(STR_SPEED_PROMOTED), static_cast<unsigned long>(kTurns - n), kTurns);
  put(line);
  snprintf(line, sizeof(line), "%s: %lu ms  (wave %lu)", tr(STR_SPEED_CLEAN),
           static_cast<unsigned long>(clean.totalUs / 1000), static_cast<unsigned long>(clean.waveUs / 1000));
  put(line);
  snprintf(line, sizeof(line), "%s: %lu ms  (wave %lu)", tr(STR_SPEED_FULL),
           static_cast<unsigned long>(full.totalUs / 1000), static_cast<unsigned long>(full.waveUs / 1000));
  put(line);
  snprintf(line, sizeof(line), "%s: %lu ms", tr(STR_SPEED_DRAW), static_cast<unsigned long>(drawUs / 1000));
  put(line);
  const auto& prof = display.profile();
  snprintf(line, sizeof(line), "%s  CPU %lu MHz  heap %lu",
           prof.controllerIsUc8279 ? "UC8279" : (prof.controllerIsSsd1677 ? "SSD1677" : "UC8253"),
           static_cast<unsigned long>(getCpuFrequencyMhz()), static_cast<unsigned long>(ESP.getFreeHeap()));
  put(line);
  put(CROSSPOINT_VERSION);
  y += lh;
  put(tr(STR_SPEED_TEST_PHOTO));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
