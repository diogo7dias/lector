add_executable(SettingsGridTest
  SettingsGridTest.cpp
)

target_include_directories(SettingsGridTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SettingsGridTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SettingsGridTest)
