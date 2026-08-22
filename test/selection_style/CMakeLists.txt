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
  BASE_THEME_SOURCE="${REPO_ROOT}/src/components/themes/BaseTheme.cpp"
)

target_link_libraries(SelectionAuditTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SelectionAuditTest)
