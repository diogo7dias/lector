add_executable(TextEncodingTest
  TextEncodingTest.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(TextEncodingTest PRIVATE
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(TextEncodingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TextEncodingTest)
