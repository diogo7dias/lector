add_executable(IdlePrewarmTimingTest IdlePrewarmTimingTest.cpp)

target_include_directories(IdlePrewarmTimingTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(IdlePrewarmTimingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(IdlePrewarmTimingTest)
