add_executable(WakeClassifyTest
  WakeClassifyTest.cpp
)

target_include_directories(WakeClassifyTest PRIVATE
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(WakeClassifyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WakeClassifyTest)
