add_executable(SettingsListNavTest
  SettingsListNavTest.cpp
)

target_include_directories(SettingsListNavTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SettingsListNavTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SettingsListNavTest)
