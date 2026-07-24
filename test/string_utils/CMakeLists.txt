add_executable(StringUtilsTest
  StringUtilsTest.cpp
)

target_include_directories(StringUtilsTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(StringUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StringUtilsTest)
