add_executable(BookSearchTest
  BookSearchTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
)

target_include_directories(BookSearchTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(BookSearchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookSearchTest)
