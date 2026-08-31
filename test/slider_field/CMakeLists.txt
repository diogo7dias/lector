add_executable(SliderFieldTest
  SliderFieldTest.cpp
)

target_include_directories(SliderFieldTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SliderFieldTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SliderFieldTest)
