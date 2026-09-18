add_executable(PowerReportTest
  PowerReportTest.cpp
)

target_include_directories(PowerReportTest PRIVATE
  ${REPO_ROOT}/lib/PowerReport
)

target_link_libraries(PowerReportTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PowerReportTest)
