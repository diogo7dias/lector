add_executable(reader_progress_save_debouncer_tests
  ReaderProgressSaveDebouncerTest.cpp
)

target_include_directories(reader_progress_save_debouncer_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(reader_progress_save_debouncer_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(reader_progress_save_debouncer_tests)
