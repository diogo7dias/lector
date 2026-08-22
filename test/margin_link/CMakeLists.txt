add_executable(MarginLinkTest
  MarginLinkTest.cpp
)

target_include_directories(MarginLinkTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(MarginLinkTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MarginLinkTest)
