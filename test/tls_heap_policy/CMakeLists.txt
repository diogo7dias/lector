add_executable(TlsHeapPolicyTest
  TlsHeapPolicyTest.cpp
)

target_include_directories(TlsHeapPolicyTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(TlsHeapPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TlsHeapPolicyTest)
