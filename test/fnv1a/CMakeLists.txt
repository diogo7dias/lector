add_executable(Fnv1aTest Fnv1aTest.cpp)
target_link_libraries(Fnv1aTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(Fnv1aTest)
