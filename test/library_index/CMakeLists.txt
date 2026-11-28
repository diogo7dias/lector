add_executable(LibraryIndexTest
  LibraryIndexTest.cpp
  LibraryProgressViewTest.cpp
  LibrarySeriesTest.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryBuilder.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryIndexFile.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryGroups.cpp
  ${REPO_ROOT}/lib/LibraryIndex/LibraryFormat.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(LibraryIndexTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${CMAKE_CURRENT_SOURCE_DIR}/../fs_helpers/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/LibraryIndex
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LibraryIndexTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LibraryIndexTest)
