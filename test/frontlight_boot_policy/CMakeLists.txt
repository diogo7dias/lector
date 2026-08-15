add_executable(FrontlightBootPolicyTest
  FrontlightBootPolicyTest.cpp
)

target_include_directories(FrontlightBootPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(FrontlightBootPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FrontlightBootPolicyTest)
