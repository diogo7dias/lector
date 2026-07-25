add_executable(BookFilingTest
  BookFilingNamesTest.cpp
  ${REPO_ROOT}/src/util/BookFilingNames.cpp
)

target_include_directories(BookFilingTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BookFilingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookFilingTest)
