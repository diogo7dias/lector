add_executable(ShrinkToFitTest ShrinkToFitTest.cpp)
# The firmware builds without exceptions; shrink_to_fit() is a no-op only there.
target_compile_options(ShrinkToFitTest PRIVATE -fno-exceptions)
target_link_libraries(ShrinkToFitTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ShrinkToFitTest)
