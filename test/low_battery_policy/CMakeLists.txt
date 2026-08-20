add_executable(LowBatteryPolicyTest
  LowBatteryPolicyTest.cpp
)

target_include_directories(LowBatteryPolicyTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(LowBatteryPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LowBatteryPolicyTest)
