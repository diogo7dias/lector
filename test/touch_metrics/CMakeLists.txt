add_executable(TouchMetricsTest
  TouchMetricsTest.cpp
)

target_include_directories(TouchMetricsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(TouchMetricsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TouchMetricsTest)
