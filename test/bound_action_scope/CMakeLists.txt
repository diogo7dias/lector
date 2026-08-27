add_executable(BoundActionScopeTest
  BoundActionScopeTest.cpp
)

target_include_directories(BoundActionScopeTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(BoundActionScopeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BoundActionScopeTest)
