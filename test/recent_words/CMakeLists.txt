add_executable(RecentWordsTest
  RecentWordsTest.cpp
)

target_include_directories(RecentWordsTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(RecentWordsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RecentWordsTest)
