add_executable(list_window_policy_tests
  ListWindowPolicyTest.cpp
)
target_link_libraries(list_window_policy_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(list_window_policy_tests PRIVATE
  ${REPO_ROOT}/src
)
gtest_discover_tests(list_window_policy_tests)
