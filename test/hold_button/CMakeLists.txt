add_executable(HoldButtonPolicyTest
  HoldButtonPolicyTest.cpp
)

target_include_directories(HoldButtonPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(HoldButtonPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HoldButtonPolicyTest)
