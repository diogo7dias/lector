add_executable(low_memory_render_tier_tests
  LowMemoryRenderTierTest.cpp
)

target_link_libraries(low_memory_render_tier_tests PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(low_memory_render_tier_tests PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(low_memory_render_tier_tests)
