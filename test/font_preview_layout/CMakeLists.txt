add_executable(font_preview_layout_tests FontPreviewLayoutTest.cpp)

target_link_libraries(font_preview_layout_tests PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(font_preview_layout_tests PRIVATE ${REPO_ROOT}/src)
gtest_discover_tests(font_preview_layout_tests)
