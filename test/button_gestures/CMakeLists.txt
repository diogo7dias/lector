add_executable(ButtonGesturesTest
  ButtonGesturesTest.cpp
)

target_include_directories(ButtonGesturesTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ButtonGesturesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ButtonGesturesTest)
