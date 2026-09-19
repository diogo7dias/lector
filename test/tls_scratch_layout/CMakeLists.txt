add_executable(TlsScratchLayoutTest
  TlsScratchLayoutTest.cpp
)

target_include_directories(TlsScratchLayoutTest PRIVATE
  ${REPO_ROOT}/src/network
)

target_link_libraries(TlsScratchLayoutTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TlsScratchLayoutTest)
