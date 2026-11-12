add_executable(WallpaperRefreshPolicyTest WallpaperRefreshPolicyTest.cpp)

target_include_directories(WallpaperRefreshPolicyTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(WallpaperRefreshPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WallpaperRefreshPolicyTest)
