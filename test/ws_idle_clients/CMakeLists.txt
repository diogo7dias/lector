add_executable(WsIdleClientsTest
  WsIdleClientsTest.cpp
)

target_include_directories(WsIdleClientsTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(WsIdleClientsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WsIdleClientsTest)
