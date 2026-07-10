add_executable(FixedBufferTest
  FixedBufferTest.cpp
)

target_include_directories(FixedBufferTest PRIVATE
  ${REPO_ROOT}/lib/Memory
)

target_link_libraries(FixedBufferTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FixedBufferTest)
