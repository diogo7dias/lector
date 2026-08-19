add_executable(BookBadgeLabelTest
  BookBadgeLabelTest.cpp
)

target_include_directories(BookBadgeLabelTest PRIVATE
  ${REPO_ROOT}/src/util
)

target_link_libraries(BookBadgeLabelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(BookBadgeLabelTest)
