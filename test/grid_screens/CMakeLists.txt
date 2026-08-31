add_executable(GridScreenAuditTest
  GridScreenAuditTest.cpp
)

target_compile_definitions(GridScreenAuditTest PRIVATE
  GRID_SCREEN_SOURCES="${REPO_ROOT}/src/activities/settings/SettingsActivity.cpp|${REPO_ROOT}/src/activities/settings/TextSettingsActivity.cpp"
)

target_link_libraries(GridScreenAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(GridScreenAuditTest)
