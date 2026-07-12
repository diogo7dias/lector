add_executable(whole_file_read_gate_tests
  WholeFileReadGateTest.cpp
)

target_link_libraries(whole_file_read_gate_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(whole_file_read_gate_tests PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(whole_file_read_gate_tests)
