add_executable(EndOfBookTest
  EndOfBookTest.cpp
  ${REPO_ROOT}/src/util/BookFilingNames.cpp
)

target_include_directories(EndOfBookTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(EndOfBookTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(EndOfBookTest)
