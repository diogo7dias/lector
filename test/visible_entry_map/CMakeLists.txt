add_executable(visible_entry_map_tests
  VisibleEntryMapTest.cpp
  ${REPO_ROOT}/src/activities/home/VisibleEntryMap.cpp
)
target_link_libraries(visible_entry_map_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(visible_entry_map_tests PRIVATE
  ${REPO_ROOT}/src
)
gtest_discover_tests(visible_entry_map_tests)
