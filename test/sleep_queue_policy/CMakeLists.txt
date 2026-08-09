add_executable(SleepQueuePolicyTest
  SleepQueuePolicyTest.cpp
)

target_include_directories(SleepQueuePolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepQueuePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepQueuePolicyTest)
