add_executable(SleepFavoritesTest
  FavoriteImageNamesTest.cpp
  SleepImageMoveTest.cpp
  WallpaperNeighbourTest.cpp
  ${REPO_ROOT}/src/util/FavoriteImageNames.cpp
)

target_include_directories(SleepFavoritesTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(SleepFavoritesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepFavoritesTest)
