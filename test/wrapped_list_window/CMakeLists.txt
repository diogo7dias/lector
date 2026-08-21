add_executable(WrappedListWindowTest
  WrappedListWindowTest.cpp
)

target_include_directories(WrappedListWindowTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WrappedListWindowTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WrappedListWindowTest)
