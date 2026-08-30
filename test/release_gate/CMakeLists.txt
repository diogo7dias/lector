add_executable(ReleaseGateTest
  ReleaseGateTest.cpp
)

target_include_directories(ReleaseGateTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReleaseGateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReleaseGateTest)
