add_executable(PowerClockPolicyTest PowerClockPolicyTest.cpp)

target_link_libraries(PowerClockPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PowerClockPolicyTest)
