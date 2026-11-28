add_executable(PresetBundleTest PresetBundleTest.cpp)
target_link_libraries(PresetBundleTest PRIVATE GTest::gtest_main)
target_include_directories(PresetBundleTest PRIVATE ${CMAKE_SOURCE_DIR}/../src)
gtest_discover_tests(PresetBundleTest)
