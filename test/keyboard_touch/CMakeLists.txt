add_executable(KeyboardTouchAuditTest
  KeyboardTouchAuditTest.cpp
)

target_compile_definitions(KeyboardTouchAuditTest PRIVATE
  KEYBOARD_TOUCH_SOURCE="${REPO_ROOT}/src/activities/util/KeyboardEntryActivity.cpp"
)

target_link_libraries(KeyboardTouchAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeyboardTouchAuditTest)
