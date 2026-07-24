add_executable(QuoteTextTest
  QuoteTextTest.cpp
)

target_include_directories(QuoteTextTest PRIVATE
  ${REPO_ROOT}/src/activities/reader
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(QuoteTextTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(QuoteTextTest)
