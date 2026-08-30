add_executable(StatusStackTest
  StatusStackTest.cpp
)

target_include_directories(StatusStackTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(StatusStackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(StatusStackTest)
