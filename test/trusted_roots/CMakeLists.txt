add_executable(TrustedRootsTest
  TrustedRootsTest.cpp
)

target_include_directories(TrustedRootsTest PRIVATE
  ${REPO_ROOT}/lib/TrustedRoots
)

target_link_libraries(TrustedRootsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TrustedRootsTest)
