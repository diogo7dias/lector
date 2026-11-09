add_executable(BatteryPercentPollTest BatteryPercentPollTest.cpp)

target_include_directories(BatteryPercentPollTest PRIVATE ${REPO_ROOT}/lib/hal)

target_link_libraries(BatteryPercentPollTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BatteryPercentPollTest)
