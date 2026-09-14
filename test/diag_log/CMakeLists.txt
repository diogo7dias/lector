add_executable(DiagLogTest
  DiagLogTest.cpp
  ${REPO_ROOT}/lib/DiagLog/DiagLog.cpp
)

target_include_directories(DiagLogTest PRIVATE
  ${REPO_ROOT}/lib/DiagLog
)

target_link_libraries(DiagLogTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(DiagLogTest)
