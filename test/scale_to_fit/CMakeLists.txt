add_executable(ScaleToFitTest
  ScaleToFitTest.cpp
)

target_link_libraries(ScaleToFitTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ScaleToFitTest)
