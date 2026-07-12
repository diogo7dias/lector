add_executable(stats_dashboard_policy_tests
  StatsDashboardPolicyTest.cpp
)

target_include_directories(stats_dashboard_policy_tests PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(stats_dashboard_policy_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(stats_dashboard_policy_tests)
