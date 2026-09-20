add_executable(WifiSessionTest
  WifiSessionTest.cpp
  ${REPO_ROOT}/lib/DiagLog/DiagLog.cpp
  ${REPO_ROOT}/lib/WifiSession/WifiSession.cpp
)

target_include_directories(WifiSessionTest PRIVATE ${REPO_ROOT}/lib/DiagLog)

target_link_libraries(WifiSessionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiSessionTest)
