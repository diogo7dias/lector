add_executable(IdlePrewarmNeighbourTest
  IdlePrewarmNeighbourTest.cpp
)

target_include_directories(IdlePrewarmNeighbourTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(IdlePrewarmNeighbourTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(IdlePrewarmNeighbourTest)
