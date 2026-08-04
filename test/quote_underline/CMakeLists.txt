add_executable(QuoteUnderlineTest
  QuoteUnderlineTest.cpp
)

target_include_directories(QuoteUnderlineTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
)

target_link_libraries(QuoteUnderlineTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(QuoteUnderlineTest)
