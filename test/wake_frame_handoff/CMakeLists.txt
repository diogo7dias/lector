add_executable(wake_frame_handoff_tests
  WakeFrameHandoffTest.cpp
  ${REPO_ROOT}/src/WakeFrameHandoff.cpp
)
target_link_libraries(wake_frame_handoff_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(wake_frame_handoff_tests PRIVATE
  ${REPO_ROOT}/src
)
gtest_discover_tests(wake_frame_handoff_tests)
