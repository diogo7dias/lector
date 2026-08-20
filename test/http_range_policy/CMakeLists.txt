add_executable(HttpRangePolicyTest
  HttpRangePolicyTest.cpp
)

target_include_directories(HttpRangePolicyTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(HttpRangePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HttpRangePolicyTest)
