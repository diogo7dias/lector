add_executable(display_refresh_policy_tests
  DisplayRefreshPolicyTest.cpp
  ${REPO_ROOT}/lib/hal/DisplayRefreshPolicy.cpp
)

target_link_libraries(display_refresh_policy_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(display_refresh_policy_tests PRIVATE ${REPO_ROOT}/lib/hal)
gtest_discover_tests(display_refresh_policy_tests)
