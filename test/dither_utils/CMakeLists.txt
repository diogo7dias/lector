add_executable(DitherUtilsTest
  DitherUtilsTest.cpp
)

target_include_directories(DitherUtilsTest PRIVATE ${REPO_ROOT}/lib/GfxRenderer)

target_link_libraries(DitherUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DitherUtilsTest)
