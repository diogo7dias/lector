add_executable(KOReaderAutoSyncPolicyTest
  KOReaderAutoSyncPolicyTest.cpp
)

target_include_directories(KOReaderAutoSyncPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(KOReaderAutoSyncPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KOReaderAutoSyncPolicyTest)
