add_executable(KOReaderMergeTest
  KOReaderMergeTest.cpp
)

target_include_directories(KOReaderMergeTest PRIVATE
  ${REPO_ROOT}/lib/KOReaderSync
)

target_link_libraries(KOReaderMergeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderMergeTest)
