add_executable(OtaBootEntryTest
  OtaBootEntryTest.cpp
)

target_include_directories(OtaBootEntryTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(OtaBootEntryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(OtaBootEntryTest)
