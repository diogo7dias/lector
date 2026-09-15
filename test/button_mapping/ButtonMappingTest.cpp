#include <gtest/gtest.h>

#include <algorithm>

#include "ButtonMapping.h"

using namespace button_mapping;

TEST(ButtonMapping, AllFrontRemapsPreserveFixedKeysAndNavigationOrder) {
  Config config{{0, 1, 2, 3}, 0, 0, true};
  do {
    for (unsigned role = 0; role < 4; ++role) {
      EXPECT_EQ(resolve(static_cast<Button>(role), config).first, config.front[role]);
    }
    for (uint8_t orientation = 0; orientation < 4; ++orientation) {
      config.orientation = orientation;
      EXPECT_EQ(resolve(Button::Up, config).first, 4);
      EXPECT_EQ(resolve(Button::Down, config).first, 5);
      EXPECT_EQ(resolve(Button::Power, config).first, 6);
      const auto next = resolve(Button::NavNext, config);
      const auto prev = resolve(Button::NavPrevious, config);
      EXPECT_EQ(next.first, orientation >= 2 ? 4 : 5);
      EXPECT_EQ(next.second, config.front[orientation >= 2 ? 2 : 3]);
      EXPECT_EQ(prev.first, orientation >= 2 ? 5 : 4);
      EXPECT_EQ(prev.second, config.front[orientation >= 2 ? 3 : 2]);
    }
  } while (std::next_permutation(config.front, config.front + 4));
}

TEST(ButtonMapping, SideLayoutAffectsOnlyPageNavigation) {
  Config config{{0, 1, 2, 3}, 0, 0, false};
  for (unsigned layout = 0; layout <= UINT8_MAX; ++layout) {
    config.sideLayout = layout;
    EXPECT_EQ(resolve(Button::PageBack, config).first, layout < 2 ? 4 + layout : NONE);
    EXPECT_EQ(resolve(Button::PageForward, config).first, layout < 2 ? 5 - layout : NONE);
    EXPECT_EQ(resolve(Button::NavNext, config).first, 5);
    EXPECT_EQ(resolve(Button::Up, config).first, 4);
  }
}

TEST(ButtonMapping, ScreenDirectionsAndLabelsUseTheLiveOrientation) {
  constexpr Button expected[][4] = {
      {Button::Left, Button::Right, Button::Up, Button::Down},
      {Button::Down, Button::Up, Button::Left, Button::Right},
      {Button::Right, Button::Left, Button::Down, Button::Up},
      {Button::Up, Button::Down, Button::Right, Button::Left},
  };
  Config config{{3, 2, 1, 0}, 0, 0, true};
  for (uint8_t orientation = 0; orientation < 4; ++orientation) {
    config.orientation = orientation;
    for (unsigned direction = 0; direction < 4; ++direction) {
      const Button screen = static_cast<Button>(static_cast<unsigned>(Button::ScreenLeft) + direction);
      EXPECT_EQ(screenDirection(screen, config), expected[orientation][direction]);
      EXPECT_EQ(resolve(screen, config).first, resolve(expected[orientation][direction], config).first);
      config.followOrientation = false;
      EXPECT_EQ(screenDirection(screen, config), expected[0][direction]);
      EXPECT_FALSE(navDirectionSwapped(config));
      config.followOrientation = true;
    }
  }
  config.orientation = UINT8_MAX;
  EXPECT_EQ(screenDirection(Button::ScreenLeft, config), Button::Left);
  EXPECT_EQ(resolve(static_cast<Button>(999), config).first, NONE);
}
