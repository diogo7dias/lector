add_executable(ListHoldNavTest
  ListHoldNavTest.cpp
)

target_include_directories(ListHoldNavTest PRIVATE
  ${REPO_ROOT}/src
)

target_compile_definitions(ListHoldNavTest PRIVATE
  BUTTON_NAVIGATOR_HEADER="${REPO_ROOT}/src/util/ButtonNavigator.h"
  BUTTON_NAVIGATOR_SOURCE="${REPO_ROOT}/src/util/ButtonNavigator.cpp"
  LIST_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/UiListActivity.cpp"
  STATUS_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/UiStatusActivity.cpp"
  BROWSER_ACTIVITY_SOURCE="${REPO_ROOT}/src/activities/home/FileBrowserActivity.cpp"
)

target_link_libraries(ListHoldNavTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListHoldNavTest)
