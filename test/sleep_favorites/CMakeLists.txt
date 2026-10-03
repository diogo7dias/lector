add_executable(SleepFavoritesTest
  FavoriteImageNamesTest.cpp
  SleepImageMoveTest.cpp
  WallpaperNeighbourTest.cpp
  ${REPO_ROOT}/src/util/FavoriteImageNames.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(SleepFavoritesTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/test/fs_helpers/stubs
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(SleepFavoritesTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SleepFavoritesTest)
