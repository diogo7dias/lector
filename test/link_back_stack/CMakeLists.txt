add_executable(LinkBackStackTest LinkBackStackTest.cpp)

target_include_directories(LinkBackStackTest PRIVATE ${REPO_ROOT}/src)

target_link_libraries(LinkBackStackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LinkBackStackTest)
