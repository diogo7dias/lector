add_executable(ButtonRouterTest
  ButtonRouterTest.cpp
)

target_include_directories(ButtonRouterTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ButtonRouterTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ButtonRouterTest)
