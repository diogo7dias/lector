add_executable(ListChromeLayoutTest
  ListChromeLayoutTest.cpp
)

target_include_directories(ListChromeLayoutTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(ListChromeLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ListChromeLayoutTest)
