add_executable(ListSwipeTest
  ListSwipeTest.cpp
)

target_include_directories(ListSwipeTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListSwipeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListSwipeTest)
