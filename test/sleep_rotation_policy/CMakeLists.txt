add_executable(SleepRotationPolicyTest
  SleepRotationPolicyTest.cpp
)

target_include_directories(SleepRotationPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepRotationPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepRotationPolicyTest)
