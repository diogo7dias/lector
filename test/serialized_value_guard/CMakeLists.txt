add_executable(SerializedValueGuardTest SerializedValueGuardTest.cpp)
target_include_directories(SerializedValueGuardTest PRIVATE
  ${REPO_ROOT}/test/css_parser/stubs
  ${REPO_ROOT}/test/opds_parser/stubs
  ${REPO_ROOT}/lib/Serialization
)
target_link_libraries(SerializedValueGuardTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SerializedValueGuardTest)
