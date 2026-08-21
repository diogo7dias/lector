add_executable(FetchUrlPolicyTest
  FetchUrlPolicyTest.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(FetchUrlPolicyTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/network
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(FetchUrlPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FetchUrlPolicyTest)
