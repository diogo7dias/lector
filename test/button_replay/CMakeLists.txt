add_executable(ButtonReplayTest
  ButtonReplayTest.cpp
)

target_include_directories(ButtonReplayTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ButtonReplayTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ButtonReplayTest)
