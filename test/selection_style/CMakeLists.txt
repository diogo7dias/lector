add_executable(SelectionAuditTest
  SelectionAuditTest.cpp
)

target_compile_definitions(SelectionAuditTest PRIVATE
  SELECTION_SOURCES="${REPO_ROOT}/src/components/themes/BaseTheme.cpp"
)

target_link_libraries(SelectionAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SelectionAuditTest)
