add_executable(ClockZonesTest ClockZonesTest.cpp)
target_link_libraries(ClockZonesTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(ClockZonesTest PRIVATE ${REPO_ROOT}/lib/hal)
gtest_discover_tests(ClockZonesTest)
