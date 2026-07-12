add_executable(sleep_favorite_move_tests
  SleepFavoriteMoveTest.cpp
)

target_link_libraries(sleep_favorite_move_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(sleep_favorite_move_tests PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(sleep_favorite_move_tests)
