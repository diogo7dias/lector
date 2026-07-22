add_executable(layout_params_tests
  LayoutParamsTest.cpp
)

target_link_libraries(layout_params_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(layout_params_tests PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub
)

gtest_discover_tests(layout_params_tests)
