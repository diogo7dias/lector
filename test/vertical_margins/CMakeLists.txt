add_executable(VerticalMarginsTest
  VerticalMarginsTest.cpp
)

target_include_directories(VerticalMarginsTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(VerticalMarginsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(VerticalMarginsTest)
