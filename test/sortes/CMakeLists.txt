add_executable(SortesTest
  SortesTest.cpp
  ${REPO_ROOT}/src/util/Sortes.cpp
  ${REPO_ROOT}/src/util/BookProgressFile.cpp
  ${REPO_ROOT}/src/util/BookFilingNames.cpp
  ${REPO_ROOT}/src/util/BusyTick.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_include_directories(SortesTest PRIVATE
  stubs
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/src
)
target_compile_definitions(SortesTest PRIVATE SORTES_REPO="${REPO_ROOT}")
target_link_libraries(SortesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SortesTest)
