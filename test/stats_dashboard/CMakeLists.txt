add_executable(StatsDashboardLayoutTest
  StatsDashboardLayoutTest.cpp
)

target_include_directories(StatsDashboardLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(StatsDashboardLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatsDashboardLayoutTest)
