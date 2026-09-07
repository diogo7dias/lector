add_executable(KeysOnlyLookTest
  KeysOnlyLookTest.cpp
)

target_compile_definitions(KeysOnlyLookTest PRIVATE
  THEME_TOKENS_HEADER="${REPO_ROOT}/src/components/UIThemeTokens.h"
  LIST_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/UiListActivity.cpp"
)

target_link_libraries(KeysOnlyLookTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeysOnlyLookTest)
