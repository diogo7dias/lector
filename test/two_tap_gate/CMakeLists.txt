add_executable(TwoTapGateTest
  TwoTapGateTest.cpp
)

target_include_directories(TwoTapGateTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(TwoTapGateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_compile_definitions(TwoTapGateTest PRIVATE
  UI_APP_HOST_SOURCE="${REPO_ROOT}/src/components/UiAppHost.cpp"
  MAPPED_INPUT_SOURCE="${REPO_ROOT}/src/MappedInputManager.cpp"
  ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/Activity.cpp"
  STATUS_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/UiStatusActivity.cpp"
  CLOCK_OFFSET_SOURCE="${REPO_ROOT}/src/activities/settings/ClockOffsetActivity.cpp"
  THEME_TOKENS_HEADER="${REPO_ROOT}/src/components/UIThemeTokens.h"
  BASE_THEME_SOURCE="${REPO_ROOT}/src/components/themes/BaseTheme.cpp"
  KEYBOARD_SOURCE="${REPO_ROOT}/src/activities/util/KeyboardEntryActivity.cpp"
  OPTION_POPUP_HEADER="${REPO_ROOT}/src/components/OptionPopup.h"
  LIGHT_PANEL_HEADER="${REPO_ROOT}/src/components/LightPanel.h"
)

gtest_discover_tests(TwoTapGateTest)
