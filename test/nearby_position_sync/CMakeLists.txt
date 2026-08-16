add_executable(NearbyPositionSyncTest
  NearbyPositionProtocolTest.cpp
  ${REPO_ROOT}/lib/NearbyPositionSync/NearbyPositionProtocol.cpp
)

target_link_libraries(NearbyPositionSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NearbyPositionSyncTest)
