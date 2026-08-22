add_executable(SelectionStyleTest
  SelectionStyleTest.cpp
)

target_include_directories(SelectionStyleTest PRIVATE
  ${REPO_ROOT}/src/components/themes
)

target_link_libraries(SelectionStyleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SelectionStyleTest)

add_executable(SelectionAuditTest
  SelectionAuditTest.cpp
)

target_compile_definitions(SelectionAuditTest PRIVATE
  SELECTION_SOURCES="${REPO_ROOT}/src/components/themes/BaseTheme.cpp|${REPO_ROOT}/src/activities/network/NearbyFileTransferActivity.cpp|${REPO_ROOT}/src/activities/browser/OpdsBookBrowserActivity.cpp|${REPO_ROOT}/src/activities/reader/XtcReaderChapterSelectionActivity.cpp"
)

target_link_libraries(SelectionAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SelectionAuditTest)
