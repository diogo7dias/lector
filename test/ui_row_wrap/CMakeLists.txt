add_executable(UiRowWrapTest
  UiRowWrapTest.cpp
)

target_include_directories(UiRowWrapTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(UiRowWrapTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(UiRowWrapTest)
