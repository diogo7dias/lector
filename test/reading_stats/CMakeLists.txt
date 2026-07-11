add_executable(reading_stats_tests
  ReadingStatsTest.cpp
  ${REPO_ROOT}/src/reading_stats/ReadingStats.cpp
  ${REPO_ROOT}/src/reading_stats/ReadingStatsStore.cpp
)

target_include_directories(reading_stats_tests PRIVATE
  ${REPO_ROOT}/src/reading_stats
)

target_link_libraries(reading_stats_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(reading_stats_tests)
