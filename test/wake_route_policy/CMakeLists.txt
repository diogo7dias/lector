add_executable(WakeRoutePolicyTest
  WakeRoutePolicyTest.cpp
)

target_include_directories(WakeRoutePolicyTest PRIVATE
  ${REPO_ROOT}/src/sleep
)

target_link_libraries(WakeRoutePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeRoutePolicyTest)
