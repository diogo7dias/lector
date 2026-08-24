add_executable(HintBandTest
  HintBandTest.cpp
)

target_include_directories(HintBandTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(HintBandTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HintBandTest)
