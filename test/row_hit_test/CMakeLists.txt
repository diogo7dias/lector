add_executable(RowHitTest
  RowHitTest.cpp
)

target_include_directories(RowHitTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(RowHitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RowHitTest)
