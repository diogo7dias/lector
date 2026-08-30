add_executable(ListScreenAuditTest
  ListScreenAuditTest.cpp
)

target_compile_definitions(ListScreenAuditTest PRIVATE
  LIST_SCREEN_SOURCES="${REPO_ROOT}/src/activities/reader/DictionaryHistoryActivity.cpp|${REPO_ROOT}/src/activities/reader/XtcReaderChapterSelectionActivity.cpp"
)

target_link_libraries(ListScreenAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListScreenAuditTest)
