add_executable(WifiSessionTest
  WifiSessionTest.cpp
  ${REPO_ROOT}/lib/WifiSession/WifiSession.cpp
)

target_link_libraries(WifiSessionTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WifiSessionTest)
