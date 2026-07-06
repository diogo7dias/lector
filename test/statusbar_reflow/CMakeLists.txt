add_executable(StatusBarReflowTest
  StatusBarReflowTest.cpp
  ${REPO_ROOT}/src/components/StatusBar.cpp
)

target_include_directories(StatusBarReflowTest PRIVATE
  ${REPO_ROOT}/src/components
)

target_link_libraries(StatusBarReflowTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatusBarReflowTest)
