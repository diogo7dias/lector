add_executable(GridNavTest
  GridNavTest.cpp
)

target_include_directories(GridNavTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(GridNavTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GridNavTest)
