add_executable(SliderBandTest
  SliderBandTest.cpp
)

target_include_directories(SliderBandTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SliderBandTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SliderBandTest)
