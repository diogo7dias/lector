add_executable(InputArmingPolicyTest
  InputArmingPolicyTest.cpp
)

target_include_directories(InputArmingPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(InputArmingPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(InputArmingPolicyTest)
