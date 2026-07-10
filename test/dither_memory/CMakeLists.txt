add_executable(DitherMemoryTest
  DitherMemoryTest.cpp
)

target_include_directories(DitherMemoryTest PRIVATE
  ${REPO_ROOT}/lib/GfxRenderer
)

target_link_libraries(DitherMemoryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DitherMemoryTest)
