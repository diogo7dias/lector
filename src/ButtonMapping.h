#pragma once

#include <cstdint>

namespace button_mapping {
enum class Button {
  Back,
  Confirm,
  Left,
  Right,
  Up,
  Down,
  Power,
  PageBack,
  PageForward,
  NavNext,
  NavPrevious,
  ScreenLeft,
  ScreenRight,
  ScreenUp,
  ScreenDown
};

// Front roles are Back, Confirm, Left, Right. Orientation is the LIVE renderer
// orientation (0 portrait, 1 CW, 2 inverted, 3 CCW), never the reader preference.
struct Config {
  uint8_t front[4];
  uint8_t sideLayout;
  uint8_t orientation;
  bool followOrientation;
};

constexpr uint8_t NONE = UINT8_MAX;
struct Hardware {
  uint8_t first = NONE;
  uint8_t second = NONE;
};

constexpr bool navDirectionSwapped(const Config& config) {
  return config.followOrientation && (config.orientation == 2 || config.orientation == 3);
}

constexpr Button screenDirection(const Button button, const Config& config) {
  // Rows follow GfxRenderer::Orientation's declared order.
  constexpr Button directions[][4] = {
      {Button::Left, Button::Right, Button::Up, Button::Down},
      {Button::Down, Button::Up, Button::Left, Button::Right},
      {Button::Right, Button::Left, Button::Down, Button::Up},
      {Button::Up, Button::Down, Button::Right, Button::Left},
  };
  if (button < Button::ScreenLeft || button > Button::ScreenDown) return button;
  const uint8_t orientation = config.followOrientation && config.orientation < 4 ? config.orientation : 0;
  return directions[orientation][static_cast<unsigned>(button) - static_cast<unsigned>(Button::ScreenLeft)];
}

constexpr Hardware resolve(Button button, const Config& config) {
  button = screenDirection(button, config);
  switch (button) {
    case Button::Back:
      return {config.front[0]};
    case Button::Confirm:
      return {config.front[1]};
    case Button::Left:
      return {config.front[2]};
    case Button::Right:
      return {config.front[3]};
    case Button::Up:
      return {4};
    case Button::Down:
      return {5};
    case Button::Power:
      return {6};
    case Button::PageBack:
      if (config.sideLayout == 0) return {4};
      if (config.sideLayout == 1) return {5};
      return {};
    case Button::PageForward:
      if (config.sideLayout == 0) return {5};
      if (config.sideLayout == 1) return {4};
      return {};
    case Button::NavNext:
      return navDirectionSwapped(config) ? Hardware{4, config.front[2]} : Hardware{5, config.front[3]};
    case Button::NavPrevious:
      return navDirectionSwapped(config) ? Hardware{5, config.front[3]} : Hardware{4, config.front[2]};
    default:
      return {};
  }
}
}  // namespace button_mapping
