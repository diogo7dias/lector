add_executable(UiSliderDialogTest
  UiSliderDialogTest.cpp
)

target_include_directories(UiSliderDialogTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(UiSliderDialogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UiSliderDialogTest)
