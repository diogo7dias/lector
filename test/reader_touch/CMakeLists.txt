add_executable(ReaderTouchTest
  ReaderTouchTest.cpp
)

target_include_directories(ReaderTouchTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReaderTouchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderTouchTest)
