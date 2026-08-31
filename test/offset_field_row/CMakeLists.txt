add_executable(OffsetFieldRowTest
  OffsetFieldRowTest.cpp
)

target_include_directories(OffsetFieldRowTest PRIVATE
  ${REPO_ROOT}/src
)

target_compile_definitions(OffsetFieldRowTest PRIVATE
  CLOCK_OFFSET_SOURCE="${REPO_ROOT}/src/activities/settings/ClockOffsetActivity.cpp"
)

target_link_libraries(OffsetFieldRowTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OffsetFieldRowTest)
