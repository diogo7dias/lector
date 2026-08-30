add_executable(ListScreenAuditTest
  ListScreenAuditTest.cpp
)

target_compile_definitions(ListScreenAuditTest PRIVATE
  LIST_SCREEN_SOURCES="${REPO_ROOT}/src/activities/reader/DictionaryHistoryActivity.cpp|${REPO_ROOT}/src/activities/reader/XtcReaderChapterSelectionActivity.cpp|${REPO_ROOT}/src/activities/reader/EpubReaderBookmarksActivity.cpp|${REPO_ROOT}/src/activities/reader/EpubReaderFootnotesActivity.cpp|${REPO_ROOT}/src/activities/reader/EpubReaderMenuActivity.cpp|${REPO_ROOT}/src/activities/reader/ReaderPresetsActivity.cpp|${REPO_ROOT}/src/activities/reader/EpubReaderChapterSelectionActivity.cpp|${REPO_ROOT}/src/activities/reader/StealLookActivity.cpp|${REPO_ROOT}/src/activities/settings/OpdsSettingsActivity.cpp|${REPO_ROOT}/src/activities/settings/OpdsServerListActivity.cpp|${REPO_ROOT}/src/activities/settings/InstalledFontsActivity.cpp|${REPO_ROOT}/src/activities/settings/ButtonBindingsActivity.cpp|${REPO_ROOT}/src/activities/settings/PopupItemsActivity.cpp|${REPO_ROOT}/src/activities/settings/LanguageSelectActivity.cpp|${REPO_ROOT}/src/activities/settings/KOReaderSettingsActivity.cpp|${REPO_ROOT}/src/activities/settings/StatusBarSettingsActivity.cpp|${REPO_ROOT}/src/activities/network/NetworkModeSelectionActivity.cpp|${REPO_ROOT}/src/activities/reader/QuotesViewerActivity.cpp|${REPO_ROOT}/src/activities/settings/ButtonRemapActivity.cpp"
)

target_link_libraries(ListScreenAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListScreenAuditTest)
