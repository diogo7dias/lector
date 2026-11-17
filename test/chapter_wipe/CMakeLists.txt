add_executable(ChapterWipeTest ChapterWipeTest.cpp)
target_include_directories(ChapterWipeTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(ChapterWipeTest PRIVATE GTest::gtest_main)
gtest_discover_tests(ChapterWipeTest)
