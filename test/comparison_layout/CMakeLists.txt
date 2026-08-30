add_executable(ComparisonLayoutTest
  ComparisonLayoutTest.cpp
)

target_include_directories(ComparisonLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ComparisonLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ComparisonLayoutTest)
