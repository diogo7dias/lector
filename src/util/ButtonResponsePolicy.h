#pragma once

#include <cstdint>

namespace button_response {

enum class Trigger { Press, Release };

constexpr Trigger navigationTrigger() { return Trigger::Press; }
constexpr Trigger imageMoveTrigger() { return Trigger::Release; }
constexpr Trigger longPressAwareTrigger() { return Trigger::Release; }

inline constexpr uint8_t kGrabQuoteLongPressSettingValue = 3;
inline constexpr uint32_t kGrabQuoteHoldMs = 400;

constexpr bool shouldStartGrabQuote(const uint8_t settingValue, const uint32_t heldMs) {
  return settingValue == kGrabQuoteLongPressSettingValue && heldMs >= kGrabQuoteHoldMs;
}

}  // namespace button_response
