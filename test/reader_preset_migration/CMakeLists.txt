add_executable(ReaderPresetMigrationTest
  ReaderPresetMigrationTest.cpp
)

target_include_directories(ReaderPresetMigrationTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}
)

target_link_libraries(ReaderPresetMigrationTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderPresetMigrationTest)
