add_executable(WakeFacePolicyTest
  WakeFacePolicyTest.cpp
)

target_include_directories(WakeFacePolicyTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WakeFacePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeFacePolicyTest)
