add_executable(ListSwipeTest
  ListSwipeTest.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
)

target_include_directories(ListSwipeTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
)

target_link_libraries(ListSwipeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListSwipeTest)
