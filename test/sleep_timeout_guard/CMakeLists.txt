add_executable(SleepTimeoutGuardTest
  SleepTimeoutGuardTest.cpp
)

target_include_directories(SleepTimeoutGuardTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepTimeoutGuardTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepTimeoutGuardTest)
