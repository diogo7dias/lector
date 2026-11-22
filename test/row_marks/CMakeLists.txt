add_executable(RowMarksTest
  RowMarksTest.cpp
)

target_include_directories(RowMarksTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(RowMarksTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RowMarksTest)
