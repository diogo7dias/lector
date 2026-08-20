#include "LowBatteryNoticeActivity.h"

#include <I18n.h>

#include "CrossPointState.h"
#include "HalDisplay.h"

LowBatteryNoticeActivity::LowBatteryNoticeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                   const uint16_t percent, const bool overReadingSurface)
    : Activity("LowBatteryNotice", renderer, mappedInput), percent(percent), overReadingSurface(overReadingSurface) {}

void LowBatteryNoticeActivity::onEnter() {
  Activity::onEnter();

  // Latched here rather than at the push site: this is the first moment the warning is
  // certain to be seen, and the flag must not run ahead of that.
  if (!APP_STATE.lowBatteryWarned) {
    APP_STATE.lowBatteryWarned = true;
    APP_STATE.saveToFile();
  }

  // "Battery low" plus the reading that triggered it, e.g. "Battery low (9%)".
  char buffer[64];
  snprintf(buffer, sizeof(buffer), "%s (%u%%)", I18N.get(StrId::STR_BATTERY_LOW), static_cast<unsigned>(percent));
  heading = buffer;

  const char* options[] = {I18N.get(StrId::STR_DONE)};
  noticePopup.show(heading.c_str(), options, 1, 0, [this](int) { finish(); });

  requestUpdate(true);
}

void LowBatteryNoticeActivity::render(RenderLock&& lock) {
  // The popup paints over the screen it was pushed on top of, so the page or menu
  // underneath stays visible around it and no clearScreen() is wanted here.
  if (noticePopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void LowBatteryNoticeActivity::loop() {
  if (noticePopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Dismissed with Back, or tapped outside: the warning has been seen either way.
  finish();
}
