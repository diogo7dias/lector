add_executable(frame_ink_metrics_tests
  FrameInkMetricsTest.cpp
  ${REPO_ROOT}/lib/hal/FrameInkMetrics.cpp
)

target_link_libraries(frame_ink_metrics_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(frame_ink_metrics_tests PRIVATE ${REPO_ROOT}/lib/hal)
gtest_discover_tests(frame_ink_metrics_tests)
