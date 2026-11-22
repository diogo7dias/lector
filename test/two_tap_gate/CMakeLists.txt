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
  UI_APP_HOST_HEADER="${REPO_ROOT}/src/components/UiAppHost.h"
  THEME_TOKENS_HEADER="${REPO_ROOT}/src/components/UIThemeTokens.h"
)

gtest_discover_tests(TwoTapGateTest)
