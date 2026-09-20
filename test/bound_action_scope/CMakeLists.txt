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

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME RetiredStatsSettings COMMAND ${Python3_EXECUTABLE}
  ${CMAKE_CURRENT_SOURCE_DIR}/check_retired_stats.py ${CMAKE_CXX_COMPILER})
