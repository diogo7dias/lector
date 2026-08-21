add_executable(TocHrefMatchTest
  TocHrefMatchTest.cpp
)

target_include_directories(TocHrefMatchTest PRIVATE
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(TocHrefMatchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TocHrefMatchTest)
