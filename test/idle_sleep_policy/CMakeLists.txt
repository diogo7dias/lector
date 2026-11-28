add_executable(IdleSleepPolicyTest IdleSleepPolicyTest.cpp)

target_link_libraries(IdleSleepPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(IdleSleepPolicyTest)
