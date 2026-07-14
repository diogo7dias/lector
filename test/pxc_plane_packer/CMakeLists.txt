add_executable(pxc_plane_packer_tests
  PxcPlanePackerTest.cpp
)
target_link_libraries(pxc_plane_packer_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(pxc_plane_packer_tests PRIVATE
  ${REPO_ROOT}/src
)
gtest_discover_tests(pxc_plane_packer_tests)
