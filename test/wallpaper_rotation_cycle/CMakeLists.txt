add_executable(wallpaper_rotation_cycle_tests
  WallpaperRotationCycleTest.cpp
  ${REPO_ROOT}/src/sleep/WallpaperPlaylistV2.cpp
)

target_link_libraries(wallpaper_rotation_cycle_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(wallpaper_rotation_cycle_tests PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(wallpaper_rotation_cycle_tests)
