add_executable(AdjustStripTest
  AdjustStripTest.cpp
)

target_include_directories(AdjustStripTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(AdjustStripTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AdjustStripTest)
