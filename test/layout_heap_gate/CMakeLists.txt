add_executable(layout_heap_gate_tests
  LayoutHeapGateTest.cpp
)

target_link_libraries(layout_heap_gate_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(layout_heap_gate_tests PRIVATE
  ${REPO_ROOT}/lib/Epub
)

gtest_discover_tests(layout_heap_gate_tests)
