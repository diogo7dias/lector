add_executable(ReaderLandingTest
  ReaderLandingTest.cpp
)

target_include_directories(ReaderLandingTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ReaderLandingTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ReaderLandingTest)
