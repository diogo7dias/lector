add_executable(KeyboardFieldLayoutTest
  KeyboardFieldLayoutTest.cpp
)

target_include_directories(KeyboardFieldLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_compile_definitions(KeyboardFieldLayoutTest PRIVATE
  KEYBOARD_SOURCE="${REPO_ROOT}/src/activities/util/KeyboardEntryActivity.cpp"
)

target_link_libraries(KeyboardFieldLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(KeyboardFieldLayoutTest)
