#pragma once

namespace button_response {

enum class Trigger { Press, Release };

constexpr Trigger navigationTrigger() { return Trigger::Press; }
constexpr Trigger imageMoveTrigger() { return Trigger::Release; }
constexpr Trigger longPressAwareTrigger() { return Trigger::Release; }

}  // namespace button_response
