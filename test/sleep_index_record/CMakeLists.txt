add_executable(SleepIndexRecordTest
  SleepIndexRecordTest.cpp
)

target_include_directories(SleepIndexRecordTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepIndexRecordTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepIndexRecordTest)
