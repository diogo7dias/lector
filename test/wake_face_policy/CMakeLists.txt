add_executable(WakeFacePolicyTest
  WakeFacePolicyTest.cpp
  ${REPO_ROOT}/lib/PerfStats/PerfStats.cpp
)

target_include_directories(WakeFacePolicyTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/PerfStats
)

target_link_libraries(WakeFacePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeFacePolicyTest)
