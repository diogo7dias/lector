add_executable(OtaRetryPolicyTest
  OtaRetryPolicyTest.cpp
)

target_include_directories(OtaRetryPolicyTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(OtaRetryPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaRetryPolicyTest)
