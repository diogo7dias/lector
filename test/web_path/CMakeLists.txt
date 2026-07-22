add_executable(web_path_tests
  WebPathTest.cpp
  ${REPO_ROOT}/src/util/WebPath.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
)
target_link_libraries(web_path_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(web_path_tests PRIVATE
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/FsHelpers
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
gtest_discover_tests(web_path_tests)
