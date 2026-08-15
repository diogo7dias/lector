add_executable(SleepFolderPolicyTest
  SleepFolderPolicyTest.cpp
)

target_include_directories(SleepFolderPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepFolderPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepFolderPolicyTest)
