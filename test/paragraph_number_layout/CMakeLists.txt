add_executable(ParagraphNumberLayoutTest
  ParagraphNumberLayoutTest.cpp
)

target_include_directories(ParagraphNumberLayoutTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(ParagraphNumberLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ParagraphNumberLayoutTest)
