add_executable(RuledLinesTest
  RuledLinesTest.cpp
)

target_include_directories(RuledLinesTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(RuledLinesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RuledLinesTest)
