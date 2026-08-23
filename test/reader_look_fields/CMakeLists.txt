add_executable(ReaderLookFieldsTest
  ReaderLookFieldsTest.cpp
)

target_include_directories(ReaderLookFieldsTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(ReaderLookFieldsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderLookFieldsTest)
