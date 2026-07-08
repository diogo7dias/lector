#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <esp_random.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"
#include "images/BootLogos.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Boot logo: the branded startup crest. Standard sleep modes always show one of
  // the original "READ TILL YOU DIE" skull crests (hardware RNG) so the startup
  // keeps its identity. With the "Random Logo" sleep screen the panel is already
  // showing the image it picked at lock time (any of the full logo table), so
  // reuse that exact one — unlocking then reveals the current wallpaper instead of
  // a fresh random pick.
  constexpr int kLogoSize = 384;  // multiple of 8: drawImage packs rows at width/8 bytes
  // Reuse the exact image the sleep screen picked (seamless unlock reveal) whenever
  // a logo was actually shown at lock: the "Random Logo" mode always shows one, and
  // "Random Logo + Custom" shows one only when the device slept outside the reader.
  const bool reuseSleepLogo = SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::UNTIL_DEATH ||
                              (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM_LOGO_CUSTOM &&
                               !APP_STATE.lastSleepFromReader);
  const uint8_t* logo = reuseSleepLogo ? bootlogos::kAll[APP_STATE.lastUntilDeathLogo % bootlogos::kCount]
                                       : bootlogos::kAll[esp_random() % bootlogos::kSkullCount];
  const int logoX = (pageWidth - kLogoSize) / 2;
  const int logoY = (pageHeight - kLogoSize) / 2 - 50;

  // 4-block segmented loader below the logo. Each block fills on its own e-ink
  // refresh, so the panel shows the bar filling one block at a time before Home
  // takes over. This blocks in onEnter for ~4 refreshes by design (branded boot
  // animation); the boot routing swaps in Home right after.
  constexpr int kSegments = 4;
  constexpr int segW = 44;
  constexpr int segH = 18;
  constexpr int segGap = 6;
  const int totalW = kSegments * segW + (kSegments - 1) * segGap;
  const int segX0 = (pageWidth - totalW) / 2;
  const int segY = logoY + kLogoSize + 24;

  renderer.clearScreen();
  renderer.drawImage(logo, logoX, logoY, kLogoSize, kLogoSize);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  for (int i = 0; i < kSegments; i++) {
    renderer.drawRect(segX0 + i * (segW + segGap), segY, segW, segH, 2, true);  // empty block outlines
  }
  renderer.displayBuffer();

  // Fill one block per refresh (the refresh time itself paces the animation).
  for (int i = 0; i < kSegments; i++) {
    renderer.fillRect(segX0 + i * (segW + segGap), segY, segW, segH, true);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
}
