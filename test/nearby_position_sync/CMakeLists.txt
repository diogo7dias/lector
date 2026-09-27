add_executable(NearbyPositionSyncTest
  NearbyPositionProtocolTest.cpp
  NearbyPositionSessionTest.cpp
  NearbyPositionReceiveTest.cpp
  ${REPO_ROOT}/lib/NearbyPositionSync/NearbyPositionProtocol.cpp
  ${REPO_ROOT}/lib/NearbyPositionSync/NearbyPositionSession.cpp
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/src/NearbyTransfer.cpp
)

target_include_directories(NearbyPositionSyncTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/network/NearbyTransfer/include
)

target_link_libraries(NearbyPositionSyncTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(NearbyPositionSyncTest)
