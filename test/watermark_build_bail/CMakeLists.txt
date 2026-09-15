add_executable(WatermarkBuildBailTest
  WatermarkBuildBailTest.cpp
)

target_include_directories(WatermarkBuildBailTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(WatermarkBuildBailTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WatermarkBuildBailTest)
