add_executable(PageJumpTest
  PageJumpTest.cpp
)

target_include_directories(PageJumpTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(PageJumpTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PageJumpTest)
