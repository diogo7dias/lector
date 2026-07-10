add_executable(large_folder_index_policy_tests
  LargeFolderIndexPolicyTest.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/src/activities/home/LibrarySearchSupport.cpp
)
target_link_libraries(large_folder_index_policy_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(large_folder_index_policy_tests PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/FsHelpers
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
gtest_discover_tests(large_folder_index_policy_tests)
