add_executable(ReadingPercentTest
  ReadingPercentTest.cpp
)

target_include_directories(ReadingPercentTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(ReadingPercentTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingPercentTest)
