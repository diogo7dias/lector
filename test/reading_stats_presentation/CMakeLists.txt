add_executable(ReadingStatsPresentationTest
  ReadingStatsPresentationTest.cpp
  ${REPO_ROOT}/src/reading_stats/ReadingStats.cpp
  ${REPO_ROOT}/src/reading_stats/ReadingStatsPresentation.cpp
)

target_include_directories(ReadingStatsPresentationTest PRIVATE
  ${REPO_ROOT}/src/reading_stats
)

target_link_libraries(ReadingStatsPresentationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReadingStatsPresentationTest)
