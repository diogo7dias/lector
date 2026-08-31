add_executable(ActivityBusyLabelTest
  ActivityBusyLabelTest.cpp
)

target_include_directories(ActivityBusyLabelTest PRIVATE
  ${REPO_ROOT}/lib/I18n
)

target_link_libraries(ActivityBusyLabelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ActivityBusyLabelTest)
