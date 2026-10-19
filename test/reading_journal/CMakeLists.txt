add_executable(ReadingJournalTest ReadingJournalTest.cpp)
target_include_directories(ReadingJournalTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/hal)
target_link_libraries(ReadingJournalTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingJournalTest)
