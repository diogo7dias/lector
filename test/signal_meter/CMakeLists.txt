add_executable(SignalMeterTest
  SignalMeterTest.cpp
)

target_include_directories(SignalMeterTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SignalMeterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SignalMeterTest)
