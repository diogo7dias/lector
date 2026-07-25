add_executable(LibrarySearchTest
  LibrarySearchTest.cpp
  ${REPO_ROOT}/src/activities/home/LibrarySearch.cpp
)

target_include_directories(LibrarySearchTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LibrarySearchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LibrarySearchTest)
