add_executable(FavoriteQueueChainTest
  FavoriteQueueChainTest.cpp
)

target_include_directories(FavoriteQueueChainTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(FavoriteQueueChainTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FavoriteQueueChainTest)
