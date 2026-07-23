add_executable(ReaderPrefsTest
  ReaderPrefsTest.cpp
)

target_include_directories(ReaderPrefsTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(ReaderPrefsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderPrefsTest)
