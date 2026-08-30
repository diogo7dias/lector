add_executable(TouchActOnceAuditTest
  TouchActOnceAuditTest.cpp
)

target_compile_definitions(TouchActOnceAuditTest PRIVATE
  TOUCH_ACT_ONCE_SOURCES="${REPO_ROOT}/src/activities/settings/SettingsActivity.cpp|${REPO_ROOT}/src/activities/settings/TextSettingsActivity.cpp"
)

target_link_libraries(TouchActOnceAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TouchActOnceAuditTest)
