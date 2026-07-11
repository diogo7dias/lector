add_executable(image_viewer_patch_tests
  ImageViewerPatchTest.cpp
  ${REPO_ROOT}/src/activities/home/ImageViewerPatch.cpp
)
target_link_libraries(image_viewer_patch_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(image_viewer_patch_tests PRIVATE
  ${REPO_ROOT}/src
)
gtest_discover_tests(image_viewer_patch_tests)
