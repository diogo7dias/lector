add_executable(TableSpanTest
  TableSpanTest.cpp
)

target_include_directories(TableSpanTest PRIVATE
  ${REPO_ROOT}/lib/Epub
)

target_link_libraries(TableSpanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TableSpanTest)
