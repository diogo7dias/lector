add_executable(ViewableInsetsTest
  ViewableInsetsTest.cpp
)

target_link_libraries(ViewableInsetsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ViewableInsetsTest)
