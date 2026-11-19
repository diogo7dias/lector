add_executable(BuiltinSizesTest BuiltinSizesTest.cpp)
target_include_directories(BuiltinSizesTest PRIVATE ${REPO_ROOT}/src ${REPO_ROOT}/lib/EpdFont)
target_link_libraries(BuiltinSizesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BuiltinSizesTest)
