add_executable(HeaderTitleTest
  HeaderTitleTest.cpp
)

target_include_directories(HeaderTitleTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(HeaderTitleTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HeaderTitleTest)
