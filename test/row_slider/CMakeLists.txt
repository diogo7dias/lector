add_executable(RowSliderTest
  RowSliderTest.cpp
)

target_include_directories(RowSliderTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(RowSliderTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RowSliderTest)
