#include "SpeedTestActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <PerfStats.h>

#include <cstdio>

#include "fontIds.h"
#ifdef FREEINK_UC8279_X4_CONFIG
#include "platform/LectorUc8279Config.h"
#endif

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

const char* SpeedTestActivity::waveName(const SweepStep& s) {
  static char buf[8];
  if (s.wave == 0) return "OTP";
  if (s.wave == 1) return "X3 DU";
  snprintf(buf, sizeof(buf), "P%u", s.frames);
  return buf;
}

void SpeedTestActivity::onEnter() {
  Activity::onEnter();
  requestUpdate();
}

void SpeedTestActivity::loop() {
  int x = 0;
  int y = 0;
  if (phase == Phase::Bench || busy) return;
  const bool back = mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (!back && !mappedInput.wasAnyPressed() && !mappedInput.wasScreenTapped(x, y)) return;
  if (back || phase == Phase::Table || (phase == Phase::Results && !canSweep())) {
    finish();
    return;
  }
  if (phase == Phase::Results) {
    phase = Phase::Sweep;
  } else if (++step == kSweepSteps) {
    phase = Phase::Table;
  }
  busy = true;
  requestUpdate();
}

void SpeedTestActivity::render(RenderLock&&) {
  switch (phase) {
    case Phase::Bench:
      runBench();
      phase = Phase::Results;
      drawResults();
      break;
    case Phase::Results:
      drawResults();
      break;
    case Phase::Sweep:
      runSweepStep();
      break;
    case Phase::Table:
      drawSweepTable();
      break;
  }
  busy = false;
}

void SpeedTestActivity::drawTestPage(const bool shifted, const char* header) const {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID);
  renderer.drawText(UI_12_FONT_ID, 10, 36, header, true);
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
  drawTestPage(false, tr(STR_SPEED_TEST_RUNNING));
  drawUs = micros() - t0;
  refresh(HalDisplay::HALF_REFRESH);  // a clean baseline, so every turn starts from the same glass
  for (int i = 0; i < kTurns; ++i) {
    drawTestPage(i % 2 == 0, tr(STR_SPEED_TEST_RUNNING));
    turns[i] = refresh(HalDisplay::FAST_REFRESH);
  }
  drawTestPage(false, tr(STR_SPEED_TEST_RUNNING));
  clean = refresh(HalDisplay::HALF_REFRESH);
  drawTestPage(true, tr(STR_SPEED_TEST_RUNNING));
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
  if (canSweep()) put(tr(STR_SPEED_SWEEP_START));
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}

bool SpeedTestActivity::canSweep() {
#ifdef FREEINK_UC8279_X4_CONFIG
  return display.profile().controllerIsUc8279 && !display.profile().isX3;
#else
  return false;
#endif
}

void SpeedTestActivity::runSweepStep() {
#ifdef FREEINK_UC8279_X4_CONFIG
  using freeink::Uc8279FastWave;
  const SweepStep& s = kSweep[step];
  char header[64];
  snprintf(header, sizeof(header), "%s %d/%d  %s", tr(STR_SPEED_SWEEP_STEP), step + 1, kSweepSteps, waveName(s));

  // Every step starts from the same clean glass (Half runs the OTP clean waveform).
  drawTestPage(true, header);
  refresh(HalDisplay::HALF_REFRESH);

  freeink::lectorUc8279SetFastWave(static_cast<Uc8279FastWave>(s.wave), s.frames);
  uint32_t total = 0, wave = 0, n = 0;
  for (int i = 0; i < kSweepTurns; ++i) {
    drawTestPage(i % 2 == 0, i + 1 == kSweepTurns ? tr(STR_SPEED_SWEEP_NEXT) : header);
    if (i + 1 == kSweepTurns)
      renderer.drawText(UI_12_FONT_ID, 10, 36 + renderer.getLineHeight(UI_12_FONT_ID), header, true);
    const Pass p = refresh(HalDisplay::FAST_REFRESH);
    if (p.mode != HalDisplay::FAST_REFRESH) continue;  // promoted: ran the stock clean waveform
    ++n;
    total += p.totalUs / 1000;
    wave += p.waveUs / 1000;
  }
  // The stock waveform stays live outside the sweep, the final table included.
  freeink::lectorUc8279SetFastWave(Uc8279FastWave::Otp);
  sweep[step] = {static_cast<uint16_t>(n ? total / n : 0), static_cast<uint16_t>(n ? wave / n : 0),
                 static_cast<uint8_t>(kSweepTurns - n)};
  LOG_INF("SPEED", "sweep %s: turn %u ms wave %u ms promoted %u", waveName(s), sweep[step].totalMs, sweep[step].waveMs,
          sweep[step].promoted);
#endif
}

void SpeedTestActivity::drawSweepTable() const {
  renderer.clearScreen();
  const int lh = renderer.getLineHeight(UI_12_FONT_ID) + 6;
  int y = 40 + lh;
  char line[64];
  renderer.drawCenteredText(UI_12_FONT_ID, 36, tr(STR_SPEED_SWEEP), true);
  for (int i = 0; i < kSweepSteps; ++i) {
    snprintf(line, sizeof(line), "%d  %-6s %ums  w%u  p%u", i + 1, waveName(kSweep[i]), sweep[i].totalMs,
             sweep[i].waveMs, sweep[i].promoted);
    renderer.drawText(UI_12_FONT_ID, 16, y, line, true);
    y += lh;
  }
  renderer.drawText(UI_12_FONT_ID, 16, y, CROSSPOINT_VERSION, true);
  y += 2 * lh;
  renderer.drawText(UI_12_FONT_ID, 16, y, tr(STR_SPEED_TEST_PHOTO), true);
  renderer.displayBuffer(HalDisplay::HALF_REFRESH);
}
