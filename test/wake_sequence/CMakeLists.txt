add_executable(WakeSequenceTest
  WakeSequenceTest.cpp
  WakeSequenceEquivalenceTest.cpp
)

target_include_directories(WakeSequenceTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WakeSequenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeSequenceTest)
