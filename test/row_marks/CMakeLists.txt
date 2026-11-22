add_executable(RowMarksTest
  RowMarksTest.cpp
  RowMarksListTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)

target_include_directories(RowMarksTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)

target_compile_options(RowMarksTest PRIVATE -fno-exceptions -fno-rtti)

target_link_libraries(RowMarksTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(RowMarksTest)
