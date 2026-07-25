add_executable(ReaderPresetsTest
  ReaderPresetNamesTest.cpp
  ${REPO_ROOT}/src/ReaderPresetNames.cpp
)

target_include_directories(ReaderPresetsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReaderPresetsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderPresetsTest)
