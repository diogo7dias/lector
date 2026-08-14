add_executable(ListScrollPolicyTest
  ListScrollPolicyTest.cpp
)

target_include_directories(ListScrollPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListScrollPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListScrollPolicyTest)
