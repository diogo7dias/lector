add_executable(SilentRestartTest SilentRestartTest.cpp)

target_include_directories(SilentRestartTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(SilentRestartTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SilentRestartTest)
