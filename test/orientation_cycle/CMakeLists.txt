add_executable(OrientationCycleTest
  OrientationCycleTest.cpp
)

target_include_directories(OrientationCycleTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(OrientationCycleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OrientationCycleTest)
