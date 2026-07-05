#include "BootActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <esp_random.h>

#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "fontIds.h"
#include "images/bootlogo0.h"
#include "images/bootlogo1.h"
#include "images/bootlogo2.h"
#include "images/bootlogo3.h"
#include "images/bootlogo4.h"

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  // Boot logo: pick one of the skull crests at random (hardware RNG, no saved
  // state). Each carries its own "READ TILL YOU DIE" text, so no brand caption.
  static const uint8_t* const kBootLogos[] = {BootLogo0, BootLogo1, BootLogo2, BootLogo3, BootLogo4};
  constexpr int kBootLogoCount = 5;
  constexpr int kLogoSize = 384;  // multiple of 8: drawImage packs rows at width/8 bytes
  // With the "Until Death" sleep screen the panel is already showing the crest it
  // picked at lock time; reuse that exact one so unlocking reveals the current
  // wallpaper instead of a fresh random crest. Any other sleep mode: random.
  const uint8_t* logo = (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::UNTIL_DEATH)
                            ? kBootLogos[APP_STATE.lastUntilDeathLogo % kBootLogoCount]
                            : kBootLogos[esp_random() % kBootLogoCount];
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
